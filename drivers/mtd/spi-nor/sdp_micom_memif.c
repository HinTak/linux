/*
 * SDP Micom memory interface driver(MEMIF)
 *
 * Copyright (C) 2016 dongseok lee <drain.lee@samsung.com>
 */

//#define VERSION_STR		"v0.1(20160805 create driver)"
//#define VERSION_STR		"v0.2(20160811 check flash remap offset in read/write)"
//#define VERSION_STR		"v0.3(20160816 jedec id translate to compatible jedec id.)"
//#define VERSION_STR		"v0.4(20160826 fix IO Unit size)"
//#define VERSION_STR		"v0.5(20160813 add custom erase)"
#define VERSION_STR		"v0.6(20161028 support disable making MTD node config)"

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#define DBGPR(fmt, arg...)	pr_debug("DBGPR %s %d: " fmt, __FUNCTION__, __LINE__, arg)

/* SDP Micom MEMIF registers, bits and macros */
#define MODE_CTRL		(0x00)
#define ERROR_LOG		(0x04)

#define SF_CLK_CON0		(0x10)
#define SF_CLK_CON1		(0x14)

#define SF_ERASE		(0x24)
#define SF_CMD			(0x28)
#define SF_MODE			(0x2C)

#define SF_ID			(0x34)
#define SPI_STM_STAT	(0x38)
#define SF_STAT_REG		(0x3C)
#define DMA_PREFETCH0	(0x40)
#define DMA_PREFETCH1	(0x44)
#define DMA_PREFETCH2	(0x48)
#define DMA_PREFETCH3	(0x4C)
#define DMA_PREFETCH4	(0x50)
#define DMA_PREFETCH5	(0x54)
#define DMA_PREFETCH6	(0x58)
#define DMA_STAT0		(0x5C)
#define DMA_STAT1		(0x60)
#define DMA_STAT2		(0x64)
#define SF_UPDATE_CON	(0x6C)

#define SPI_STM_STAT_READY		(0x1<<28)

#define SF_OWN_STATUS		(0x30)
#define SF_OWN_STATUS_CPU	(0x01)
#define SF_OWN_SET_CPU			(0x00)
#define SF_OWN_CLEAR_CPU		(0x04)

/* chunk 16byte */
#define CHUNK_SHIFT			0x4
#define CHUNK_SIZE			(0x1<<CHUNK_SHIFT)
#define CHUNK_MASK			(CHUNK_SIZE-1)

#define WRITE_ALIGN			0x4
#define WRITE_ALIGN_MASK	(WRITE_ALIGN-1)

struct sdp_mc_memif {
	struct device *dev;
	void __iomem *io_base;
	void __iomem *flash_base;
	void __iomem *sfown_base;
	struct spi_nor nor;
	struct mtd_info mtd[1];
};

#define _JEDECID_TO_ARRAY(id) { (((id)>>16)&0xFF), (((id)>>8)&0xFF), ((id)&0xFF) }
static const u8 jedecid_lookup_table[][2][3] = {
	{_JEDECID_TO_ARRAY(0xef4013), _JEDECID_TO_ARRAY(0xef3013)},/* W25Q40 to w25x40 */
};

static inline void writel_bitset(u32 value, void *addr) {
	writel(readl(addr) | value, addr);
}

static inline void writel_bitclr(u32 value, void *addr) {
	writel(readl(addr) & (~value), addr);
}

static const u8* jedecid_lookup(u8 *id) {
	int i = 0;

	for(i = 0; i < ARRAY_SIZE(jedecid_lookup_table); i++) {
		if(id[0] == jedecid_lookup_table[i][0][0] &&
			id[1] == jedecid_lookup_table[i][0][1] &&
			id[2] == jedecid_lookup_table[i][0][2]) {

			return jedecid_lookup_table[i][1];
		}
	}

	return NULL;
}

static int sdp_mc_memif_wait_for_ready(struct sdp_mc_memif *mcmif)
{
	ktime_t start, now;
	ktime_t timeout;

	start = now = ktime_get();
	timeout = ktime_add_us(start, 1000000);

	while(true) {
		if(readl(mcmif->io_base + SPI_STM_STAT)&SPI_STM_STAT_READY) {
			break;
		}
		now = ktime_get();
		if(ktime_compare(now, timeout) > 0) {
			u32 stm_stat = readl(mcmif->io_base + SPI_STM_STAT);

			if(stm_stat&&SPI_STM_STAT_READY) {/* one more check */
				break;
			}

			dev_err(mcmif->dev, "%s: timeout! stm_stat=0x%08x, wait time=%lldus\n", __FUNCTION__,
				stm_stat, ktime_us_delta(now, start));
			return -ETIMEDOUT;
		}
		yield();
	}

	return 0;
}

static int sf_own_get(struct sdp_mc_memif *mcmif) {
	ktime_t start, now;
	ktime_t timeout;

	start = now = ktime_get();
	timeout = ktime_add_us(start, 1000000);

	while(true) {
		writel(0x1, mcmif->sfown_base + SF_OWN_SET_CPU);
		if(readl(mcmif->sfown_base + SF_OWN_STATUS) == SF_OWN_STATUS_CPU) {
			break;
		}
		now = ktime_get();
		if(ktime_compare(now, timeout) > 0) {
			u32 own_status = readl(mcmif->sfown_base + SF_OWN_STATUS);

			if(own_status == SF_OWN_STATUS_CPU) {/* one more check */
				break;
			}

			dev_err(mcmif->dev, "%s: timeout! own_status=0x%08x, wait time=%lldus\n", __FUNCTION__,
				own_status, ktime_us_delta(now, start));
			return -ETIMEDOUT;
		}
		yield();
	}

	return 0;
}

static int sf_own_put(struct sdp_mc_memif *mcmif) {

	writel(0x1, mcmif->sfown_base + SF_OWN_CLEAR_CPU);
	udelay(1);
	if(readl(mcmif->sfown_base + SF_OWN_STATUS) == SF_OWN_STATUS_CPU) {
		return -ETIMEDOUT;
	}

	return 0;
}

static inline int sf_offset_get(struct sdp_mc_memif *mcmif) {
	return (readl(mcmif->io_base + SF_UPDATE_CON)>>4)&0x7;
}

static int sdp_mc_memif_prepare(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	int ret;

	ret = sf_own_get(mcmif);
	if(ret < 0) {
		dev_err(mcmif->dev, "%s: sf_own_get() return timeout!\n", __FUNCTION__);
		return ret;
	}

	return 0;
}

static void sdp_mc_memif_unprepare(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	int ret;

	ret = sf_own_put(mcmif);
	if(ret < 0) {
		dev_err(mcmif->dev, "%s: sf_own_put() return timeout!\n", __FUNCTION__);
	}
}

/* for tv screen noise workaround. */
static int sdp_chunk_memcpy(void *dst, const void *src, size_t num) {
	size_t cur = 0;
	const size_t chunks_len = (num & ~CHUNK_MASK);

	for(cur = 0; cur < chunks_len; cur += CHUNK_SIZE) {
		memcpy(dst + cur, src + cur, CHUNK_SIZE);
		udelay(1);/* for bus release */
	}

	if(num & CHUNK_MASK) {
		memcpy(dst + cur, src + cur, num & CHUNK_MASK);
	}

	return 0;
}

static int sdp_mc_memif_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	int ret = 0, i;

	DBGPR("opcode 0x%02x, len %d\n", opcode, len);

	switch(opcode) {
	case SPINOR_OP_RDSR:
		if(len >= 1) {
			writel(0x10, mcmif->io_base + SF_CMD);
			ret = sdp_mc_memif_wait_for_ready(mcmif);
			buf[0] = readl(mcmif->io_base + SF_STAT_REG)&0xFF;

			if(len == 2) {/*CR*/
				writel(0x30, mcmif->io_base + SF_CMD);
				ret = sdp_mc_memif_wait_for_ready(mcmif);
				buf[1] = readl(mcmif->io_base + SF_STAT_REG)>>8&0xFF;
			}
		}
		break;

	case SPINOR_OP_RDCR:
		if(len >= 1) {
			writel(0x30, mcmif->io_base + SF_CMD);
			ret = sdp_mc_memif_wait_for_ready(mcmif);
			buf[0] = readl(mcmif->io_base + SF_STAT_REG)>>8&0xFF;
		}
		break;

	case SPINOR_OP_RDID: {
		const u8 *new_jedecid = NULL;
		writel(0x1, mcmif->io_base + SF_CMD);
		ret = sdp_mc_memif_wait_for_ready(mcmif);
		buf[0] = readl(mcmif->io_base + SF_ID)&0xFF;
		buf[1] = readl(mcmif->io_base + SF_ID)>>8&0xFF;
		buf[2] = readl(mcmif->io_base + SF_ID)>>16&0xFF;
		new_jedecid = jedecid_lookup(buf);
		if(new_jedecid) {
			dev_info(mcmif->dev, "jedecid translated: %x%x%x -> %x%x%x\n",
				buf[0], buf[1], buf[2], new_jedecid[0], new_jedecid[1], new_jedecid[2]);

			buf[0] = new_jedecid[0];
			buf[1] = new_jedecid[1];
			buf[2] = new_jedecid[2];
		}
		break;
	}

	default:
		dev_err(mcmif->dev, "%s: not supported opcode(0x%02x)\n", __FUNCTION__, opcode);
		ret = -EINVAL;
		break;
	
	}

	for(i = 0; i < len; i++) {
		DBGPR("data[%d]=0x%02x\n", i, buf[i]);
	}

	return ret;
}

static int sdp_mc_memif_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len, int write_enable)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	int ret = 0, i;

	DBGPR("opcode 0x%02x, len %d\n", opcode, len);
	for(i = 0; i < len; i++) {
		DBGPR("data[%d]=0x%02x\n", i, buf[i]);
	}

	switch(opcode) {
	case SPINOR_OP_WREN:
	case SPINOR_OP_WRDI:
		/* notthing */
		break;

	case SPINOR_OP_WRSR:
		if(len == 1) {
			writel(0x100 | (buf[0] << 16), mcmif->io_base + SF_CMD);
			ret = sdp_mc_memif_wait_for_ready(mcmif);
		} else if(len == 2) {
			writel(0x300 | (buf[1] << 24) | (buf[0] << 16), mcmif->io_base + SF_CMD);
			ret = sdp_mc_memif_wait_for_ready(mcmif);
		} else {
			ret = -EINVAL;
		}
		break;

	default:
		dev_err(mcmif->dev, "%s: not supported opcode(0x%02x), led(%d)\n", __FUNCTION__, opcode, len);
		ret = -EINVAL;
		break;
	
	}

	return ret;
}

static int sdp_mc_memif_read(struct spi_nor *nor, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	loff_t remap_offset = 0;
	loff_t remap_from = from;

	DBGPR("from 0x%08x, len 0x%x\n", (u32)from, len);

	remap_offset = sf_offset_get(mcmif);
	if(remap_offset) {
		remap_offset = 0x10000 * (0x1 << remap_offset);

		if(from < remap_offset) {
			remap_from = from + remap_offset;
			if((from + len) > remap_offset) {
				dev_err(mcmif->dev, "not supported read request!"
					"(from=0x%05llx, len=0x%x, remap_offset=0x%05llx)\n",
					from, len, remap_offset);
				return -ENOTSUPP;
			}
		} else if(from < (remap_offset*2)) {
			remap_from = from - remap_offset;
			if((from + len) > (remap_offset*2)) {
				dev_err(mcmif->dev, "not supported read request!"
					"(from=0x%05llx, len=0x%05x, remap_offset=0x%05llx)\n",
					from, len, remap_offset);
				return -ENOTSUPP;
			}
		}
	}

	sdp_chunk_memcpy(buf, mcmif->flash_base + remap_from, len);

	*retlen += len;
	return 0;
}


static void sdp_mc_memif_write(struct spi_nor *nor, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	struct sdp_mc_memif *mcmif = nor->priv;
	loff_t remap_offset = 0;
	loff_t remap_to = to;
	loff_t cur_to = to;
	const u_char *cur_buf = buf;
	size_t cur_len = len;
	int i;

	DBGPR("to 0x%08x, len 0x%x\n", (u32)to, len);

	remap_offset = sf_offset_get(mcmif);
	if(remap_offset) {
		remap_offset = 0x10000 * (0x1 << remap_offset);

		if(to < remap_offset) {
			remap_to = to + remap_offset;
			if((to + len) > remap_offset) {
				dev_err(mcmif->dev, "not supported write request!"
					"(to=0x%05llx, len=0x%05x, remap_offset=0x%05llx)\n",
					to, len, remap_offset);
				return;
			}
		} else if(to < (remap_offset*2)) {
			remap_to = to - remap_offset;
			if((to + len) > (remap_offset*2)) {
				dev_err(mcmif->dev, "not supported write request!"
					"(to=0x%05llx, len=0x%05x, remap_offset=0x%05llx)\n",
					to, len, remap_offset);
				return;
			}
		}
	}

	if(remap_to&WRITE_ALIGN_MASK) {
		u8 buf_start[WRITE_ALIGN];

		sdp_chunk_memcpy(buf_start, mcmif->flash_base + (remap_to&~WRITE_ALIGN_MASK), WRITE_ALIGN);

		for(i = (remap_to&WRITE_ALIGN_MASK); (i < len) && (i < WRITE_ALIGN); i++) {
			buf_start[i] = buf[i - (remap_to&WRITE_ALIGN_MASK)];
		}

		writel_bitset(0x10, mcmif->io_base + MODE_CTRL);//set download mode
		sdp_chunk_memcpy(mcmif->flash_base + (remap_to&~WRITE_ALIGN_MASK), buf_start, WRITE_ALIGN);
		writel_bitclr(0x10, mcmif->io_base + MODE_CTRL);//clear download mode

		cur_to += i-(remap_to&WRITE_ALIGN_MASK);
		cur_buf += i-(remap_to&WRITE_ALIGN_MASK);
		cur_len -= i-(remap_to&WRITE_ALIGN_MASK);
	}

	/* start address aligned */
	if(cur_len&~WRITE_ALIGN_MASK) {
		if((cur_to&0xFF) == 0 && (cur_len&0xFF) == 0)
			writel_bitset(0x2, mcmif->io_base + SF_UPDATE_CON);

		writel_bitset(0x10, mcmif->io_base + MODE_CTRL);//set download mode
		sdp_chunk_memcpy(mcmif->flash_base + cur_to, cur_buf, cur_len&~WRITE_ALIGN_MASK);
		writel_bitclr(0x10, mcmif->io_base + MODE_CTRL);//clear download mode

		if((cur_to&0xFF) == 0 && (cur_len&0xFF) == 0)
			writel_bitclr(0x2, mcmif->io_base + SF_UPDATE_CON);

		cur_to += cur_len&~WRITE_ALIGN_MASK;
		cur_buf += cur_len&~WRITE_ALIGN_MASK;
		cur_len -= cur_len&~WRITE_ALIGN_MASK;
	}

	if(cur_len) {
		u8 buf_end[4];

		sdp_chunk_memcpy(buf_end, mcmif->flash_base + cur_to, WRITE_ALIGN);

		for(i = 0; i < (cur_len&WRITE_ALIGN_MASK); i++) {
			buf_end[i] = cur_buf[(cur_len&~WRITE_ALIGN_MASK) + i];
		}

		writel_bitset(0x10, mcmif->io_base + MODE_CTRL);//set download mode
		sdp_chunk_memcpy(mcmif->flash_base + cur_to, buf_end, WRITE_ALIGN);
		writel_bitclr(0x10, mcmif->io_base + MODE_CTRL);//clear download mode
	}

	*retlen += len;
}

static int __sdp_mc_memif_erase(struct sdp_mc_memif *mcmif, u8 erase_opcode, loff_t offs)
{
	int ret;

	DBGPR("erase_opcode 0x%02x, offs 0x%x\n", erase_opcode, (u32)offs);

	switch(erase_opcode) {
	case SPINOR_OP_BE_4K:/* Erase 4KiB block */
		writel((0x10<<24) | (offs), mcmif->io_base + SF_ERASE);
		ret = sdp_mc_memif_wait_for_ready(mcmif);
		break;

	case SPINOR_OP_SE:/* Sector erase (usually 64KiB) */
		writel((0x11<<24) | (offs), mcmif->io_base + SF_ERASE);
		ret = sdp_mc_memif_wait_for_ready(mcmif);
		break;

	case SPINOR_OP_BE_32K:/* Erase 32KiB block */
		writel((0x12<<24) | (offs), mcmif->io_base + SF_ERASE);
		ret = sdp_mc_memif_wait_for_ready(mcmif);
		break;

	case SPINOR_OP_CHIP_ERASE:/* Erase whole flash chip */
		writel((0x13<<24) | (offs), mcmif->io_base + SF_ERASE);
		ret = sdp_mc_memif_wait_for_ready(mcmif);
		break;

	default:
		dev_err(mcmif->dev, "%s: not supported opcode(0x%02x), led(%u)\n",
			__FUNCTION__, erase_opcode, (u32)offs);
		return -EINVAL;
		break;
	
	}

	return 0;
}

static int sdp_mc_memif_erase(struct spi_nor *nor, loff_t offs)
{
	struct sdp_mc_memif *mcmif = nor->priv;

	return __sdp_mc_memif_erase(mcmif, nor->erase_opcode, offs);
}

#ifdef CONFIG_SDP_MICOM_MEMIF_USE_COSTOM_ERASE
/* copy from spi-nor.c, and modified */

#define MAX_READY_WAIT_JIFFIES  (40 * HZ) /* M25P16 specs 40s max chip erase */

/*
* Read the status register, returning its value in the location
* Return the status register value.
* Returns negative if error occurred.
*/
static int read_sr(struct spi_nor *nor)
{
		int ret;
		u8 val;

		ret = nor->read_reg(nor, SPINOR_OP_RDSR, &val, 1);
		if (ret < 0) {
				pr_err("sdp_spi_nor_custom_erase: error %d reading SR\n", (int) ret);
				return ret;
		}

		return val;
}

static inline int spi_nor_sr_ready(struct spi_nor *nor)
{
		int sr = read_sr(nor);
		if (sr < 0)
				return sr;
		else
				return !(sr & SR_WIP);
}
static int spi_nor_ready(struct spi_nor *nor)
{
		int sr;
		sr = spi_nor_sr_ready(nor);
		if (sr < 0)
				return sr;
		return sr;
}
static int spi_nor_wait_till_ready(struct spi_nor *nor)
{
		unsigned long deadline;
		int timeout = 0, ret;

		deadline = jiffies + MAX_READY_WAIT_JIFFIES;

		while (!timeout) {
				if (time_after_eq(jiffies, deadline))
						timeout = 1;

				ret = spi_nor_ready(nor);
				if (ret < 0)
						return ret;
				if (ret)
						return 0;

				cond_resched();
		}

		dev_err(nor->dev, "sdp_spi_nor_custom_erase: flash operation timed out\n");

		return -ETIMEDOUT;
}

static int sdp_spi_nor_custom_erase(struct mtd_info *mtd, struct erase_info *instr)
{
		struct spi_nor *nor = mtd->priv;
		struct sdp_mc_memif *mcmif = nor->priv;

		u32 addr, len;
		uint32_t rem;
		int ret = 0;

		dev_dbg(nor->dev, "at 0x%llx, len %lld\n", (long long)instr->addr,
						(long long)instr->len);

		div_u64_rem(instr->len, mtd->erasesize, &rem);
		if (rem)
				return -EINVAL;

		addr = instr->addr;
		len = instr->len;

		mutex_lock(&nor->lock);

		if (nor->prepare) {
				ret = nor->prepare(nor, SPI_NOR_OPS_ERASE);
				if (ret) {
						dev_err(nor->dev, "sdp_spi_nor_custom_erase: : failed in the preparation.\n");
						mutex_unlock(&nor->lock);
						return ret;
				}
		}


		/* whole-chip erase? */
		if (len == mtd->size) {
				unsigned long timeout;

				//write_enable(nor);
				nor->write_reg(nor, SPINOR_OP_WREN, NULL, 0, 0);

				//if (erase_chip(nor)) {
				if(__sdp_mc_memif_erase(mcmif, SPINOR_OP_CHIP_ERASE, addr)) {
						ret = -EIO;
						goto erase_err;
				}

				ret = spi_nor_wait_till_ready(nor);
				if (ret)
						goto erase_err;

		/* "sector"-at-a-time erase */
		} else {
				u8 erase_opcode = nor->erase_opcode;
				uint32_t erasesize = mtd->erasesize;

				/* 64kb erase erase */
				if(mtd->erasesize != 0x10000 && !(addr&0xFFFF) && !(len&0xFFFF)) {
						erase_opcode = SPINOR_OP_SE;
						erasesize = 0x10000;

				/* 32kb erase erase */
				} else if(mtd->erasesize != 0x8000 && !(addr&0x7FFF) && !(len&0x7FFF)) {
						erase_opcode = SPINOR_OP_BE_32K;
						erasesize = 0x8000;
				}

				while (len) {
						//write_enable(nor);
						nor->write_reg(nor, SPINOR_OP_WREN, NULL, 0, 0);

						//ret = spi_nor_erase_sector(nor, addr);
						ret = __sdp_mc_memif_erase(mcmif, erase_opcode, addr);
						if (ret)
								goto erase_err;

						addr += erasesize;
						len -= erasesize;

						ret = spi_nor_wait_till_ready(nor);
						if (ret)
								goto erase_err;
				}
		}

		//write_disable(nor);
		nor->write_reg(nor, SPINOR_OP_WRDI, NULL, 0, 0);

erase_err:
		if (nor->unprepare)
				nor->unprepare(nor, SPI_NOR_OPS_ERASE);

		mutex_unlock(&nor->lock);

		instr->state = ret ? MTD_ERASE_FAILED : MTD_ERASE_DONE;
		mtd_erase_callback(instr);

		return ret;
}
#endif

static int sdp_mc_memif_setup_flash(struct sdp_mc_memif *mcmif,
				 struct device_node *np)
{
	enum read_mode flash_read;
	int ret;

#if 0
	u32 property;
	u16 mode = 0;

	if (!of_property_read_u32(np, "spi-rx-bus-width", &property)) {
		switch (property) {
		case 1:
			break;
		case 2:
			mode |= SPI_RX_DUAL;
			break;
		case 4:
			mode |= SPI_RX_QUAD;
			break;
		default:
			dev_err(mcmif->dev, "unsupported rx-bus-width\n");
			return -EINVAL;
		}
	}

	if (of_find_property(np, "spi-cpha", NULL))
		mode |= SPI_CPHA;

	if (of_find_property(np, "spi-cpol", NULL))
		mode |= SPI_CPOL;

	if (mode & SPI_RX_DUAL) {
		ctrl |= MEMIF_CTRL_DUAL;
		flash_read = SPI_NOR_DUAL;
	} else if (mode & SPI_RX_QUAD) {
		ctrl &= ~MEMIF_CTRL_DUAL;
		flash_read = SPI_NOR_QUAD;
	} else {
		ctrl |= MEMIF_CTRL_DUAL;
		flash_read = SPI_NOR_NORMAL;
	}
#endif

	flash_read = SPI_NOR_NORMAL;

	mcmif->nor.dev   = mcmif->dev;
	mcmif->nor.priv  = mcmif;
	mcmif->nor.mtd->dev.of_node = np;
	mcmif->nor.mtd->priv = &mcmif->nor;
	mcmif->nor.read  = sdp_mc_memif_read;
	mcmif->nor.write = sdp_mc_memif_write;
	mcmif->nor.read_reg  = sdp_mc_memif_read_reg;
	mcmif->nor.write_reg = sdp_mc_memif_write_reg;
	mcmif->nor.erase = sdp_mc_memif_erase;
	mcmif->nor.prepare = sdp_mc_memif_prepare;
	mcmif->nor.unprepare = sdp_mc_memif_unprepare;


	ret = spi_nor_scan(&mcmif->nor, NULL, flash_read);
	if (ret) {
		dev_err(mcmif->dev, "device scan failed\n");
		return ret;
	}

#ifdef CONFIG_SDP_MICOM_MEMIF_USE_COSTOM_ERASE
	/* use sdp custom erase logic */
	dev_info(mcmif->dev, "use costom erase logic\n");
	mcmif->nor.mtd->_erase = sdp_spi_nor_custom_erase;
#endif


#ifndef CONFIG_SDP_MICOM_MEMIF_DISABLE_MTD_NODE
	{
		struct mtd_part_parser_data ppdata;
		ppdata.of_node = np;
		ret = mtd_device_parse_register(mcmif->nor.mtd, NULL, &ppdata, NULL, 0);
		if (ret) {
			dev_err(mcmif->dev, "mtd device parse failed\n");
			return ret;
		}
	}
#else
	dev_info(mcmif->dev, "disable making mtd device node\n");
#endif

	return 0;
}

static struct mtd_info *mtd = NULL;
struct mtd_info *sdp_mc_memif_mtd_get(void) {
	if(mtd == NULL) {
		pr_err("sdp-mc-memif: memif mtd info is NULL!\n");
	}
	return mtd;
}
EXPORT_SYMBOL(sdp_mc_memif_mtd_get);

extern int sdp_mc_ctrl_syscall_get_version(u8 target, u16 *api, u16 *drv);

static int sdp_mc_memif_check_compatible(struct platform_device *pdev) {
	int ret;
	u16 api = 0, drv = 0;

	ret = sdp_mc_ctrl_syscall_get_version(1, &api, &drv);

	if(ret < 0) {
		dev_err(&pdev->dev, "cat not get micom driver version!!\n");
		return ret;
	}

	if(api < 0x0100 || drv < 0x0100) {
		dev_err(&pdev->dev, "micom driver version is lower!!(api %.4x, drv %.4x)\n", api, drv);
		return -ENOTSUPP;
	}

	return 0;
}

static int sdp_mc_memif_probe(struct platform_device *pdev)
{
	struct device_node *flash_np;
	struct sdp_mc_memif *mcmif;
	struct resource *res;
	int ret;

	ret = sdp_mc_memif_check_compatible(pdev);
	if(ret < 0) {
		return -ENOTSUPP;
	}

	mcmif = devm_kzalloc(&pdev->dev, sizeof(*mcmif), GFP_KERNEL);
	if (!mcmif)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "memif");
	mcmif->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mcmif->io_base))
		return PTR_ERR(mcmif->io_base);
	DBGPR("mcmif->io_base %p\n", mcmif->io_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "flash");
	mcmif->flash_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mcmif->flash_base))
		return PTR_ERR(mcmif->flash_base);
	DBGPR("mcmif->flash_base %p\n", mcmif->flash_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sfown");
	mcmif->sfown_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mcmif->sfown_base))
		return PTR_ERR(mcmif->sfown_base);
	DBGPR("mcmif->sfown_base %p\n", mcmif->sfown_base);

	mcmif->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcmif);

	/* Initialize and reset device */
	mcmif->nor.mtd = mcmif->mtd;

	flash_np = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!flash_np) {
		dev_err(&pdev->dev, "no SPI flash device in device tree\n");
		ret = -ENODEV;
		goto _err;
	}

	ret = sdp_mc_memif_setup_flash(mcmif, flash_np);
	if (ret) {
		dev_err(&pdev->dev, "unable to setup flash chip\n");
		goto _err;
	}

	/* export for mtd api */
	mtd = mcmif->nor.mtd;

	dev_info(&pdev->dev, "probe done.%s\n", VERSION_STR);

	return 0;

_err:
	return ret;
}

static int sdp_mc_memif_remove(struct platform_device *pdev)
{
	struct sdp_mc_memif *mcmif = platform_get_drvdata(pdev);

	mtd_device_unregister(mcmif->nor.mtd);

	return 0;
}

static const struct of_device_id sdp_mc_memif_match[] = {
	{.compatible = "samsung,sdp-mc-memif"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdp_mc_memif_match);

static struct platform_driver sdp_mc_memif_driver = {
	.probe	= sdp_mc_memif_probe,
	.remove	= sdp_mc_memif_remove,
	.driver	= {
		.name = "sdp-mc-memif",
		.of_match_table = sdp_mc_memif_match,
	},
};
module_platform_driver(sdp_mc_memif_driver);

MODULE_DESCRIPTION("SDP Micom memory interface driver");
MODULE_AUTHOR("dongseok lee <drain.lee@samsung.com>");
MODULE_LICENSE("GPL v2");
