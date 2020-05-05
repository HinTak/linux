
/******************************************************************
*		File : sdp_mc_ctrl.c
*		Description : Micom control driver
*		Author : drain.lee@samsung.com
*		Date : 2015/07/08
*******************************************************************/
/*
 * 2015.07.08,drain.lee: create source file.
 * 2015.08.11,drain.lee: add EW Command.
 * 2015.08.18,drain.lee: add channel buffer
 * 2015.10.24,drain.lee: add EWCMD Retry code.
 * 2016.02.23,drain.lee: fix SVACE defects(99126, 99127, 116960)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/circ_buf.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/soc.h>

#define DRIVER_HANDLER_NAME "sdp-micom-ctrl"
#define DRIVER_HANDLER_VER  "160223"


#define SDP_MC_CTRL_M0_DATA	0x08
#define SDP_MC_CTRL_M0_PEND	0x40
#define SDP_MC_CTRL_M0_CLEAR	0x44

#define SDP_MC_CTRL_CPU_DATA		0x80
#define SDP_MC_CTRL_CPU_PEND		0x48
#define SDP_MC_CTRL_CPU_CLEAR	0x4C

#define SDP_MC_CTRL_MAX_INT_NUM	0x8
#define SDP_MC_CTRL_BUF_SIZE 0x100

#define CTRL_PEND_ALL			(0xFF)
#define CTRL_PEND_EWCMD			(0x1<<0)
#define CTRL_PEND_DEBUGPRINT	(0x1<<6)
#define CTRL_PEND_DEBUGINFO		(0x1<<7)

typedef void (*sdp_mc_ctrl_callback_func)(unsigned char *buf, unsigned int size, void *arg);

struct sdp_mc_ctrl;

/* channel ring buffer */
struct sdp_mc_ringbuf_t {
	spinlock_t lock;
	unsigned long head;
	unsigned long tail;

	struct {
		unsigned long long time;
		unsigned long buf[6];
	} data[SDP_MC_CTRL_BUF_SIZE];
};

struct sdp_mc_ctrl_channel_t {
	struct sdp_mc_ctrl_t *mccrtl;
	int chanid;
	struct dentry *dbg_root;

	void *arg;
	sdp_mc_ctrl_callback_func func;

	struct sdp_mc_ringbuf_t *ringbuf;
};

/* private handler device data */
struct sdp_mc_ctrl_t {
	/* dev resource */
	struct platform_device *pdev;
	struct resource *region_res;
	void __iomem *base;
	int irq;
	struct cpumask irq_affinity;
	spinlock_t lock;

	/* per interrupt callback */
	struct sdp_mc_ctrl_channel_t channel[SDP_MC_CTRL_MAX_INT_NUM];
};


static DEFINE_MUTEX(write_mutex);

static void sdp_mc_ringbuf_init(struct sdp_mc_ringbuf_t *ringbuf)
{
	spin_lock_init(&ringbuf->lock);
	ringbuf->head = 0;
	ringbuf->tail = 0;

	return;
}

static int sdp_mc_ringbuf_enqueue(struct sdp_mc_ringbuf_t *ringbuf, unsigned long in_data[6])
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&ringbuf->lock, flags);

	if(CIRC_SPACE(ringbuf->head, ringbuf->tail, SDP_MC_CTRL_BUF_SIZE) >= 1) {
		ringbuf->data[ringbuf->head].time = sched_clock();
		memcpy(ringbuf->data[ringbuf->head].buf, in_data, 4*6);
		ringbuf->head = (ringbuf->head + 1) & (SDP_MC_CTRL_BUF_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static int sdp_mc_ringbuf_dequeue(struct sdp_mc_ringbuf_t *ringbuf, unsigned long out_data[6])
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&ringbuf->lock, flags);

	if(CIRC_CNT(ringbuf->head, ringbuf->tail, SDP_MC_CTRL_BUF_SIZE) >= 1) {
		memcpy(out_data, ringbuf->data[ringbuf->tail].buf, 4*6);
		ringbuf->tail = (ringbuf->tail + 1) & (SDP_MC_CTRL_BUF_SIZE-1);
	} else {
		ret = -ENOSPC;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&ringbuf->lock, flags);
	return ret;
}

static irqreturn_t sdp_mc_ctrl_isr(int irq, void* dev_id)
{
	struct sdp_mc_ctrl_t *mccrtl = (struct sdp_mc_ctrl_t *)dev_id;
	unsigned int u32GicSync;
	unsigned int pending_bit;
	int chan = -1;

	if(unlikely(!mccrtl)) {
		pr_err("%s %d: Handler ISR: host is not allocated!", __FUNCTION__, __LINE__);
		return IRQ_NONE;
	}

	pending_bit = readl(mccrtl->base + SDP_MC_CTRL_M0_PEND);

	if(!pending_bit) {
		dev_err(&mccrtl->pdev->dev, "No pending bit\n");
		return IRQ_NONE;
	} else {
		int bit = 0;
		int nr_pend = 0;
		/* check num of pending bit.. */
		for(bit = 0; bit < 8; bit++) {
			if(pending_bit & (0x1UL<<bit)) {
				nr_pend++;
				if(chan < 0) {
					chan = bit;
				} else {
					dev_err(&mccrtl->pdev->dev, "multiple pending bit(%02x), ignored\n", pending_bit);
					writel(pending_bit, mccrtl->base + SDP_MC_CTRL_M0_CLEAR);
					u32GicSync = readl(mccrtl->base + SDP_MC_CTRL_M0_CLEAR);
					return IRQ_HANDLED;
				}
			}
		}
	}


	if ((0x1<<chan) & pending_bit) {
		if(!mccrtl->channel[chan].func) {
			dev_err(&mccrtl->pdev->dev, "not registered channel(%d) (pend %02x)\n", chan, pending_bit);
		} else {
			unsigned long buf[6];
			int ret;

			buf[0] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x00);
			buf[1] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x04);
			buf[2] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x08);
			buf[3] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x0C);
			buf[4] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x10);
			buf[5] = readl(mccrtl->base + SDP_MC_CTRL_M0_DATA + 0x14);
			
			ret = sdp_mc_ringbuf_enqueue(mccrtl->channel[chan].ringbuf, buf);

			writel(pending_bit, mccrtl->base + SDP_MC_CTRL_M0_CLEAR);
			u32GicSync = readl(mccrtl->base + SDP_MC_CTRL_M0_CLEAR);

			if(ret < 0) {
				dev_err(&mccrtl->pdev->dev, "channel%d ring buffer is full(%d). data corrupted!\n", chan, ret);
				return IRQ_HANDLED;
			}
		}
	}

	return IRQ_WAKE_THREAD;
}


static irqreturn_t sdp_mc_ctrl_thread(int irq, void* dev_id)
{
	struct sdp_mc_ctrl_t *mccrtl = (struct sdp_mc_ctrl_t *)dev_id;
	int chan = 0;

	if(unlikely(!mccrtl)) {
		pr_err("%s %d: Handler Thread: host is not allocated!", __FUNCTION__, __LINE__);
		return IRQ_NONE;
	}

	for(chan = 0; chan < SDP_MC_CTRL_MAX_INT_NUM; chan++) {
		unsigned long buf[6];
		if(mccrtl->channel[chan].ringbuf) {
			while(sdp_mc_ringbuf_dequeue(mccrtl->channel[chan].ringbuf, buf) >= 0) {
				mccrtl->channel[chan].func((unsigned char *)buf, 4*6, mccrtl->channel[chan].arg);
			}
		}
	}
	return IRQ_HANDLED;
}

struct sdp_mc_ctrl_channel_t * sdp_mc_ctrl_register_channel(struct sdp_mc_ctrl_t *mccrtl, int chanid, sdp_mc_ctrl_callback_func func, void *arg)
{
	struct sdp_mc_ctrl_channel_t *chan = NULL;
	if(!mccrtl) {
		return NULL;
	}

	if(!(chanid >= 0 && chanid < SDP_MC_CTRL_MAX_INT_NUM)) {
		dev_err(&mccrtl->pdev->dev, "not supported channel %d\n", chanid);
		return NULL;
	}

	chan = &mccrtl->channel[chanid];

	if(chan->func) {
		dev_err(&mccrtl->pdev->dev, "already registered channel %d\n", chanid);
		return NULL;
	}

	chan->ringbuf = kzalloc(sizeof(struct sdp_mc_ringbuf_t), GFP_KERNEL);
	sdp_mc_ringbuf_init(chan->ringbuf);

	chan->mccrtl = mccrtl;
	chan->chanid = chanid;
	chan->func = func;
	chan->arg = arg;

	return chan;
}

EXPORT_SYMBOL(sdp_mc_ctrl_register_channel);

int sdp_mc_ctrl_send_nonblcok(struct sdp_mc_ctrl_channel_t *chan, unsigned int size,  unsigned char *buf)
{
	struct sdp_mc_ctrl_t *mccrtl = NULL;
	unsigned int pending_bit;
	unsigned long flags;

	if(!chan) {
		return -EINVAL;
	}
	if(size > 32) {
		return -EINVAL;
	}

	mccrtl = chan->mccrtl;

	spin_lock_irqsave(&mccrtl->lock, flags);

	pending_bit = readl(mccrtl->base + SDP_MC_CTRL_CPU_PEND);

	if(pending_bit) {
		spin_unlock_irqrestore(&mccrtl->lock, flags);
		return -EBUSY;
	}

	memcpy(mccrtl->base + SDP_MC_CTRL_CPU_DATA, buf, size);

	writel(0x1 << chan->chanid, mccrtl->base + SDP_MC_CTRL_CPU_PEND);

	spin_unlock_irqrestore(&mccrtl->lock, flags);

	return size;
}
EXPORT_SYMBOL(sdp_mc_ctrl_send_nonblcok);

int sdp_mc_ctrl_send_cancel(struct sdp_mc_ctrl_channel_t *chan)
{
	struct sdp_mc_ctrl_t *mccrtl = NULL;
	unsigned long flags;

	if(!chan) {
		return -EINVAL;
	}

	mccrtl = chan->mccrtl;

	spin_lock_irqsave(&mccrtl->lock, flags);

	if((0x1 << chan->chanid) & readl(mccrtl->base + SDP_MC_CTRL_CPU_PEND)) {
		writel(0x1 << chan->chanid, mccrtl->base + SDP_MC_CTRL_CPU_CLEAR);
	} else {
		dev_err(&mccrtl->pdev->dev, "channel%d cancel cmd is ignored\n", chan->chanid);
	}
	spin_unlock_irqrestore(&mccrtl->lock, flags);

	return 0;
}
EXPORT_SYMBOL(sdp_mc_ctrl_send_cancel);

/****************** Debug Print *******************/
typedef struct {
	uint8_t size;
	char	string[31];
} Soc_DEBUG_PRINT_t;
#define SOC_DBG_PRINT_MAX_STRING_CPU (31-8)
#define SOC_DBG_PRINT_MAX_STRING_M0 (31-8)

void sdp_mc_ctrl_callback_dbg_print(unsigned char *buf, unsigned int size, void *data)
{
	Soc_DEBUG_PRINT_t *dbg_print = (Soc_DEBUG_PRINT_t *)buf;
	char string[32];
	static int is_line_start = true;

	if(dbg_print->size > SOC_DBG_PRINT_MAX_STRING_CPU) {
		pr_err("Micom DEBUG PRINT: invalid size %d\n", dbg_print->size);
	} else {
		int in, out;
		for(in = 0, out = 0; in < dbg_print->size; in++) {
			if(is_line_start) {
				printk(KERN_INFO "Micom: ");
				is_line_start = false;
			}

			string[out++] = dbg_print->string[in];

			if(dbg_print->string[in] == '\n') {
				string[out] = '\0';
				printk("%s", string);
				out = 0;
				is_line_start = true;
			}
		}
		string[out] = '\0';
		printk("%s", string);
	}
}

/********************* EW Command **********************/
typedef struct {
	uint8_t cmd;
	uint8_t size;/* Data Size */
	uint8_t type;/* 0 SEND, 1 ACK*/
	uint8_t idx;/* index increase */
	char	data[28];
} Soc_EW_CMD_INFO_t;

struct sdp_mc_ewcmd_priv_t {
	Soc_EW_CMD_INFO_t cmdinfo;
	struct mutex lock;
	struct completion done;
	char *debug_level;
};

typedef struct {
	uint16_t result;
	uint16_t data_size;
	uint16_t status_org;
	uint16_t status_edited;
	uint16_t status_read_us;
	uint16_t status_write_us;
	uint16_t data_erase_us;
	uint16_t data_write_us;
	uint16_t data_read_us;
} Soc_EW_FlashTest_t;

#define SOC_EW_CMD_DO_CLOCKGATING				0x1
#define SOC_EW_CMD_GET_CLOCKGATING_COUNT		0x2
#define SOC_EW_CMD_DO_FLASH_TEST				0x3
#define SOC_EW_CMD_DO_MAIN_RESET				0x4
#define SOC_EW_CMD_DO_DBG_PRINT_TEST			0xFF

#define SOC_EW_TYPE_SEND						0x0
#define SOC_EW_TYPE_ACK_OK						0x1
#define SOC_EW_TYPE_ACK_ERR						0x2

#define SOC_EW_CMD_DO_MAIN_RESET_COUNT			0x10

void sdp_mc_ctrl_callback_ew_cmd(unsigned char *buf, unsigned int size, void *data)
{
	struct sdp_mc_ewcmd_priv_t *priv = data;
	Soc_EW_CMD_INFO_t *ewcmd = (Soc_EW_CMD_INFO_t *)buf;

	if(priv->debug_level) {
		printk("%sEWCMD: Recive cmd:%u, size:%u, type:%u, idx:%u\n", priv->debug_level, ewcmd->cmd, ewcmd->size, ewcmd->type, ewcmd->idx);
		print_hex_dump(priv->debug_level, "EWCMD: Data(Hex) ", DUMP_PREFIX_OFFSET, 16, 1, ewcmd->data, 28-8, false);
	}

	if(ewcmd->type == SOC_EW_TYPE_ACK_OK) {
		if(ewcmd->idx == priv->cmdinfo.idx) {
			priv->cmdinfo = *ewcmd;
			complete(&priv->done);
		} else {
			pr_err("EWCMD: ACK index miss match(cmd idx %u, ack idx %u)\n", priv->cmdinfo.idx, ewcmd->idx);
		}
	} else {
		pr_err("EWCMD: error not ACK\n");
	}
}

int sdp_mc_ctrl_send_ew_cmd(struct sdp_mc_ctrl_channel_t *chan, uint8_t cmd, uint8_t size, void *data)
{
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;
	static uint8_t idx = 0;
	const unsigned int timeout_ms = 1000;
	const int retry_max = 3;
	Soc_EW_CMD_INFO_t *ewcmd = &priv->cmdinfo;
	int ret, retry = 0;
	uint8_t cur_idx = 0;

	if(size > 0 && data == NULL) {
		return -EINVAL;
	}

	if(size > ARRAY_SIZE(ewcmd->data)) {
		return -EINVAL;
	}

	mutex_lock(&priv->lock);

_retry:
	cur_idx = idx;

	ewcmd->cmd = cmd;
	ewcmd->size = size;
	ewcmd->idx = cur_idx;
	ewcmd->type = SOC_EW_TYPE_SEND;
	memcpy(ewcmd->data, data, size);

	if(priv->debug_level) {
		printk("%sEWCMD: Send cmd:%u, size:%u, type:%u, idx:%u\n", priv->debug_level, ewcmd->cmd, ewcmd->size, ewcmd->type, ewcmd->idx);
		print_hex_dump(priv->debug_level, "EWCMD: Data(Hex) ", DUMP_PREFIX_OFFSET, 16, 1, ewcmd->data, 28, false);
	}

	init_completion(&priv->done);
	ret = sdp_mc_ctrl_send_nonblcok(chan, size + 4, (void *)ewcmd);
	if(ret < 0) {
		mutex_unlock(&priv->lock);
		return ret;
	}

	idx++;

	/* wait response*/
	ret = wait_for_completion_timeout(&priv->done, msecs_to_jiffies(timeout_ms));
	if(ret == 0) {
		priv->cmdinfo.idx = idx;
		sdp_mc_ctrl_send_cancel(chan);

		if(retry < retry_max) {
			retry++;
			pr_err("EWCMD: timeout %dms! (cmd:%u, idx:%u)... do retry %d/%d!\n", timeout_ms, cmd, cur_idx, retry, retry_max);
			goto _retry;
		} else {
			pr_err("EWCMD: timeout %dms! (cmd:%u, idx:%u)... fail!\n", timeout_ms, cmd, cur_idx);
			mutex_unlock(&priv->lock);
			return -ETIMEDOUT;
		}
	}

	mutex_unlock(&priv->lock);
	return cur_idx;
}

/**************************** DebugFS ****************************/
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static int sdp_mc_ctrl_dbg_dbglog_level_set(void *data, u64 val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;

	if(val == 0) {
		priv->debug_level = NULL;
	} else {
		priv->debug_level = KERN_INFO;
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_dbglog_level_fops, NULL, sdp_mc_ctrl_dbg_dbglog_level_set,
	"%lld\n");

static int sdp_mc_ctrl_dbg_send_ewcmd_set(void *data, u64 val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;
	Soc_EW_CMD_INFO_t *ewcmd = NULL;
	int ret;

	ret = sdp_mc_ctrl_send_ew_cmd(chan, (uint8_t)(val&0xFFU), 0x0, NULL);

	if(ret < 0) {
		pr_err("EWCMD: return error %d\n", ret);
		return ret;
	}

	ewcmd = &priv->cmdinfo;
	if(ewcmd->type == SOC_EW_TYPE_ACK_OK) {

	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_send_ewcmd_fops, NULL, sdp_mc_ctrl_dbg_send_ewcmd_set,
	"%lld\n");


static int sdp_mc_ctrl_dbg_clockgating_get(void *data, u64 *val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;
	Soc_EW_CMD_INFO_t *ewcmd = NULL;
	int ret;

	ret = sdp_mc_ctrl_send_ew_cmd(chan, SOC_EW_CMD_DO_CLOCKGATING, 0x0, NULL);
	if(ret < 0) {
		pr_err("EWCMD: return error %d\n", ret);
		return ret;
	}

	ewcmd = &priv->cmdinfo;
	if(ewcmd->type == SOC_EW_TYPE_ACK_OK) {
		*val = *((uint32_t *)ewcmd->data);
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_clockgating_fops, sdp_mc_ctrl_dbg_clockgating_get, NULL,
	"%lld\n");

static int sdp_mc_ctrl_dbg_clockgating_count_get(void *data, u64 *val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;
	Soc_EW_CMD_INFO_t *ewcmd = NULL;
	int ret;

	ret = sdp_mc_ctrl_send_ew_cmd(chan, SOC_EW_CMD_GET_CLOCKGATING_COUNT, 0x0, NULL);
	if(ret < 0) {
		pr_err("EWCMD: return error %d\n", ret);
		return ret;
	}

	ewcmd = &priv->cmdinfo;
	if(ewcmd->type == SOC_EW_TYPE_ACK_OK) {
		*val = *((uint32_t *)ewcmd->data);
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_clockgating_count_fops, sdp_mc_ctrl_dbg_clockgating_count_get, NULL,
	"%lld\n");


static int sdp_mc_ctrl_dbg_flash_test_get(void *data, u64 *val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	struct sdp_mc_ewcmd_priv_t *priv = chan->arg;
	Soc_EW_CMD_INFO_t *ewcmd = NULL;
	int ret;

	ret = sdp_mc_ctrl_send_ew_cmd(chan, SOC_EW_CMD_DO_FLASH_TEST, 0x0, NULL);
	if(ret < 0) {
		pr_err("EWCMD: return error %d\n", ret);
		return ret;
	}

	ewcmd = &priv->cmdinfo;
	if(ewcmd->type == SOC_EW_TYPE_ACK_OK) {
		Soc_EW_FlashTest_t *response = (Soc_EW_FlashTest_t *)ewcmd->data;
		*val = response->result;
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_flash_test_fops, sdp_mc_ctrl_dbg_flash_test_get, NULL,
	"%lld\n");

static int sdp_mc_ctrl_dbg_main_reset_set(void *data, u64 val)
{
	struct sdp_mc_ctrl_channel_t *chan = data;
	int ret;

	if(val == 0xdeadbeef) {
		ret = sdp_mc_ctrl_send_ew_cmd(chan, SOC_EW_CMD_DO_MAIN_RESET, 0x0, NULL);
	} else {
		return -EINVAL;
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_mc_ctrl_dbg_main_reset_fops, NULL, sdp_mc_ctrl_dbg_main_reset_set,
	"%lld\n");



static void sdp_mc_ctrl_ewcmd_add_debugfs(struct sdp_mc_ctrl_channel_t *chan)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(&chan->mccrtl->pdev->dev), NULL);
	root = debugfs_create_dir("ewcmd", root);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	if (!debugfs_create_file("dbglog_level", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_dbglog_level_fops))
		goto err_node;

	if (!debugfs_create_file("send_ewcmd", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_send_ewcmd_fops))
		goto err_node;

	if (!debugfs_create_file("do_clockgate", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_clockgating_fops))
		goto err_node;

	if (!debugfs_create_file("clockgate_count", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_clockgating_count_fops))
		goto err_node;

	if (!debugfs_create_file("do_flash_test", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_flash_test_fops))
		goto err_node;

	if (!debugfs_create_file("do_main_reset", S_IRUSR|S_IWUSR, root, chan, &sdp_mc_ctrl_dbg_main_reset_fops))
		goto err_node;

	chan->dbg_root = root;

	return;

err_node:
	debugfs_remove_recursive(root);
	chan->dbg_root = NULL;
err_root:
	dev_err(&chan->mccrtl->pdev->dev, "failed to initialize debugfs\n");
}
#else
static inline void sdp_mc_ctrl_ewcmd_add_debugfs(struct sdp_mc_ctrl_channel_t *chan)
{
}
#endif/*CONFIG_DEBUG_FS*/






static int sdp_mc_ctrl_probe (struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdp_mc_ctrl_t *mccrtl = NULL;
	struct resource *plat_res, *region_res;
	int irq;
	int ret;
	u32 cpu_aff;
	char str_irqaff[32] = {0};

	/* platform get  */
	plat_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(!plat_res) {
		dev_err(dev, "platform_get_resource mem failed!\n");
		return -ENXIO;
	}
	irq = platform_get_irq(pdev, 0);
	if(irq < 0) {
		dev_err(dev, "platform_get_irq failed!\n");
		return irq;
	}


	/*  */
	if (!devres_open_group(dev, NULL, GFP_KERNEL)) {
		dev_err(dev, "devres_open_group failed!\n");
		return -ENOMEM;
	}

	mccrtl = devm_kzalloc(dev, sizeof(struct sdp_mc_ctrl_t), GFP_KERNEL);
	if(!mccrtl) {
		dev_err(dev, "mccrtl allocate failed!\n");
		ret =  -ENOMEM;
		goto probe_err;
	}

	region_res = devm_request_mem_region(dev, plat_res->start, resource_size(plat_res), pdev->name);
	if(!region_res) {
		dev_err(dev, "devm_request_mem_region failed!\n");
		ret =  -EBUSY;
		goto probe_err;
	}

	mccrtl->base = devm_ioremap(dev, region_res->start, resource_size(region_res));
	if(!mccrtl->base) {
		dev_err(dev, "devm_ioremap failed!\n");
		ret =  -EBUSY;
		goto probe_err;
	}
#if 0
	code_base = of_iomap(dev->of_node, 1);
	BUG_ON(!code_base);
#endif

	writel(CTRL_PEND_ALL, mccrtl->base + SDP_MC_CTRL_M0_CLEAR);

	ret = devm_request_threaded_irq(dev, irq, sdp_mc_ctrl_isr, sdp_mc_ctrl_thread, 0x0, pdev->name, (void*)mccrtl);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq failed\n");
		ret =  -EBUSY;
		goto probe_err;
	}

	/* Parse device tree */
	if (dev->of_node) {
		if(of_property_read_u32(dev->of_node, "irq-affinity", &cpu_aff)==0) {
			if(num_online_cpus() > 1) {
				mccrtl->irq_affinity = *cpumask_of(cpu_aff);
				irq_set_affinity(irq, cpumask_of(cpu_aff));
			}
		}
	} else {
		dev_warn(dev, "device tree node not found\n");
	}


	platform_set_drvdata(pdev, mccrtl);
	mccrtl->pdev = pdev;
	mccrtl->region_res = region_res;
	mccrtl->irq = irq;
	spin_lock_init(&mccrtl->lock);

	//bitmap_scnlistprintf(str_irqaff, ARRAY_SIZE(str_irqaff), &mccrtl->irq_affinity, NR_CPUS);
	dev_info(dev, "probe done(irq %d, irq affinity %s, ringbuf depth %d)\n", mccrtl->irq, str_irqaff, SDP_MC_CTRL_BUF_SIZE);

	devres_remove_group(dev, NULL);

	{
		struct sdp_mc_ctrl_channel_t *chan = NULL;
		struct sdp_mc_ewcmd_priv_t *priv = kzalloc(sizeof(*priv), GFP_KERNEL);

		/* XXX: temp */
		chan = sdp_mc_ctrl_register_channel(mccrtl, 6, sdp_mc_ctrl_callback_dbg_print, NULL);
		dev_info(dev, "register_channel6 : sdp_mc_ctrl_callback_dbg_print %p\n", chan);


		mutex_init(&priv->lock);
		init_completion(&priv->done);
		complete(&priv->done);
		chan = sdp_mc_ctrl_register_channel(mccrtl, 0, sdp_mc_ctrl_callback_ew_cmd, priv);
		dev_info(dev, "register_channel0 : sdp_mc_ctrl_callback_ew_cmd %p\n", chan);
		sdp_mc_ctrl_ewcmd_add_debugfs(chan);
	}

	return 0;


probe_err:
	devres_release_group(dev, NULL);
	return ret;
}


static int sdp_mc_ctrl_remove(struct platform_device *pdev)
{
	struct sdp_mc_ctrl_t *mccrtl = NULL;

	mccrtl = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);

	if(mccrtl) {
		free_irq(mccrtl->irq, mccrtl);
		iounmap(mccrtl->base);
		release_region(mccrtl->region_res->start, resource_size(mccrtl->region_res));
		kfree(mccrtl);
	}

	return 0;
}

static int sdp_mc_ctrl_suspend(struct device *dev)
{
	return 0;
}

static int sdp_mc_ctrl_resume(struct device *dev)
{
	return 0;
}

static const struct of_device_id sdp_mc_ctrl_dt_match[] = {
	//{ .compatible = "samsung,sdp-messagebox" },
	{ .compatible = "samsung,sdp-micom-ctrl" },
	{},
};
MODULE_DEVICE_TABLE(of, sdp_mc_ctrl_dt_match);

static struct dev_pm_ops sdp_mc_ctrl_pm = {
	.suspend_noirq	= sdp_mc_ctrl_suspend,
	.resume_noirq	= sdp_mc_ctrl_resume,
};


static struct platform_driver sdp_mc_ctrl_driver = {
	.probe		= sdp_mc_ctrl_probe,
	.remove 	= sdp_mc_ctrl_remove,
	.driver 	= {
		.name = "sdp-micom-ctrl",
		.pm	= &sdp_mc_ctrl_pm,

#ifdef CONFIG_OF
		.of_match_table = sdp_mc_ctrl_dt_match,
#endif
	},
};


static int __init sdp_mc_ctrl_init (void)
{
	pr_info("%s: module init. version %s\n", sdp_mc_ctrl_driver.driver.name, DRIVER_HANDLER_VER);
	return platform_driver_register(&sdp_mc_ctrl_driver);
}
arch_initcall(sdp_mc_ctrl_init);

static void __exit sdp_mc_ctrl_exit(void)
{
	pr_info("%s: module exit.", sdp_mc_ctrl_driver.driver.name);
	platform_driver_unregister(&sdp_mc_ctrl_driver);
}
module_exit(sdp_mc_ctrl_exit);


MODULE_DESCRIPTION("Samsung DTV Micom control driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("drain.lee <drain.lee@samsung.com>");


