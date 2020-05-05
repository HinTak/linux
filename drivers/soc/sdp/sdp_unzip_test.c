/*
 * Copyright 2015 Samsung Electronics Co., Ltd.
 *		Dongseok Lee <drain.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/outercache.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/math64.h>

#include <linux/crypto.h>
#include <crypto/hash.h>
//#include <crypto/rng.h>
//#include <crypto/drbg.h>
#include <crypto/compress.h>
#include <crypto/sha.h>
#include <linux/zlib.h>
#include <linux/crc32.h>


#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

//#include <mach/hw_decompress.h>
#include <mach/sdp_unzip.h>


static int nr_thread = 0;
static int err_inject_mod = 000;


#define DEBUG		1
#define N_PAGES		33/* max buffer size per one sample  */
#define N_LOOP		0
#define N_THERADS	0

#define DEFAULT_SRC_ALIGN	1
#define DEFAULT_GZIP_ALIGN	64
#define DEFAULT_SHA_MSG_ALIGN	16

#define N_PRINT_TIME_MS	3000

#define TO_ALIGNED(value, align)	(  ((value)+(align)-1) & ~((align)-1)  )
#define TEST_WINDOWBITS	12

//#define USE_GZIP_OTF_MMC_READ

#ifdef CONFIG_SDP_UNZIP_AUTH
#define USE_GZIP_DECRYPT
#define USE_GZIP_AUTH
//#define USE_GZIP_ZLIB
#endif


static DEFINE_MUTEX(fileop_mutax);

struct file* file_open(const char* path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	mutex_lock(&fileop_mutax);
	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	mutex_unlock(&fileop_mutax);
	if(IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}
	return filp;
}

void file_close(struct file* file) {
	mutex_lock(&fileop_mutax);
	filp_close(file, NULL);
	mutex_unlock(&fileop_mutax);
}

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	mutex_lock(&fileop_mutax);
	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, data, size, &offset);

	set_fs(oldfs);
	mutex_unlock(&fileop_mutax);
	return ret;
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	mutex_lock(&fileop_mutax);
	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	mutex_unlock(&fileop_mutax);
	return ret;
}

int file_sync(struct file* file) {
	mutex_lock(&fileop_mutax);
	vfs_fsync(file, 0);
	mutex_unlock(&fileop_mutax);
	return 0;
}

struct sdp_unzip_test_t {
	unsigned int src_size;

	unsigned int src_align;/* src buf*/
	unsigned int comp_align;/* gziped buf*/
	unsigned int sha_msg_align;
	int			use_zlib;
};


struct sdp_unzip_test_thread {
	struct list_head	node;
	struct task_struct	*task;
	int			id;

	unsigned int src_size;

	unsigned int src_align;
	unsigned int comp_align;
	unsigned int sha_msg_align;
	int			use_zlib;
};

const char sample_text[] = "The quick brown fox jumps over the lazy dog";
const char sample_sha256[] = {0xd7, 0xa8, 0xfb, 0xb3, 0x07, 0xd7, 0x80, 0x94, 0x69, 0xca, 0x9a, 0xbc, 0xb0, 0x08, 0x2e, 0x4f, 0x8d, 0x56, 0x51, 0xe4, 0x6d, 0x3c, 0xdb, 0x76, 0x2d, 0x02, 0xd0, 0xbf, 0x37, 0xc9, 0xe5, 0x92,};
//"\x2b\x7e\x15\x16\x28\xae\xd2\xa6\xab\xf7\x15\x88\x09\xcf\x4f\x3c";
const char sample_key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c, };
const char sample_iv[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, };

/*
	plain Text -> gziped 	-> aes ctr encypted	-> gziped	-> plain text
			+-> sha
					+-> sha
*/
struct cipher_testvec {
	char key[32];
	char iv[32];
	char *input;
	char *compressed;
	char *result;
	char *compressed_rsa_result;
	char *encypted_rsa_result;
	unsigned char fail;
	unsigned char klen;
	unsigned short ilen;
	unsigned short rlen;
};

struct sdp_gzip_test_vecter {
	unsigned int klen;
	char key[32];
	char iv[32];

	char *idata;
	char *icomp;
	char *icomp_encrypt;

	char *rsa_icomp;
	char *rsa_icomp_encrypt;


	unsigned int idata_len;
	unsigned int icomp_len;

	unsigned int fail;
};



#ifdef USE_GZIP_OTF_MMC_READ
#include <linux/blkdev.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>

enum mmc_packed_type {
	MMC_PACKED_NONE = 0,
	MMC_PACKED_WRITE,
};

struct mmc_blk_request {
	struct mmc_request	mrq;
	struct mmc_command	sbc;
	struct mmc_command	cmd;
	struct mmc_command	stop;
	struct mmc_data		data;
};

struct mmc_packed {
	struct list_head	list;
	u32			cmd_hdr[1024];
	unsigned int		blocks;
	u8			nr_entries;
	u8			retries;
	s16			idx_failure;
};

struct mmc_queue_req {
	struct request		*req;
	struct mmc_blk_request	brq;
	struct scatterlist	*sg;
	char			*bounce_buf;
	struct scatterlist	*bounce_sg;
	unsigned int		bounce_sg_len;
	struct mmc_async_req	mmc_active;
	enum mmc_packed_type	cmd_type;
	struct mmc_packed	*packed;
};

struct mmc_queue {
	struct mmc_card		*card;
	struct task_struct	*thread;
	struct semaphore	thread_sem;
	unsigned int		flags;
#define MMC_QUEUE_SUSPENDED	(1 << 0)
#define MMC_QUEUE_NEW_REQUEST	(1 << 1)

	int			(*issue_fn)(struct mmc_queue *, struct request *);
	void			*data;
	struct request_queue	*queue;
	struct mmc_queue_req	mqrq[2];
	struct mmc_queue_req	*mqrq_cur;
	struct mmc_queue_req	*mqrq_prev;
};

extern int mmc_send_status(struct mmc_card *card, u32 *status);
extern struct completion mmc_rescan_work;

static unsigned int mmch_dbg_get_capacity(struct mmc_card *card)
{
		if (!mmc_card_sd(card) && mmc_card_blockaddr(card))
				return card->ext_csd.sectors;
		else
				return card->csd.capacity << (card->csd.read_blkbits - 9);
}

static int
mmch_dbg_prepare_mrq(struct mmc_card *card, struct mmc_request *mrq, u32 blk_addr, u32 blk_count, int is_write)
{
	struct mmc_host *host = NULL;

	if(!card) {
		pr_err("mmch_dbg_prepare_mrq: card is NULL\n");
		return -EINVAL;
	}

	host = card->host;

	if(!(mrq && mrq->cmd && mrq->data && mrq->stop)) {
		pr_err("mmch_dbg_prepare_mrq: invalied arg!\n");
		if(mrq)
			pr_err("mmch_dbg_prepare_mrq: mrq %p, sbc %p, cmd %p, data %p, stop %p\n",
				mrq, mrq->sbc, mrq->cmd, mrq->data, mrq->stop);
		else
			pr_err("mmch_dbg_prepare_mrq: mrq is NULL\n");

		return -EINVAL;
	}

	if(mrq->sbc) {
		mrq->sbc->opcode = MMC_SET_BLOCK_COUNT;
		mrq->sbc->arg = blk_count;
		mrq->sbc->flags = MMC_RSP_R1B;
		if (!mmc_card_blockaddr(card))
			mrq->sbc->arg <<= 9;
	}

	mrq->cmd->opcode = is_write ? MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	mrq->cmd->arg = blk_addr;
	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	if (!mmc_card_blockaddr(card))
		mrq->cmd->arg <<= 9;

	mrq->data->blksz = 512;
	mrq->data->blocks = blk_count;
	mrq->data->flags = is_write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = NULL;
	mrq->data->sg_len = 0;

	mrq->stop->opcode = MMC_STOP_TRANSMISSION;
	mrq->stop->arg = 0;
	mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;


	mrq->cmd->error = 0;
	mrq->cmd->mrq = mrq;
	if (mrq->data) {
		BUG_ON(mrq->data->blksz > host->max_blk_size);
		BUG_ON(mrq->data->blocks > host->max_blk_count);
		BUG_ON(mrq->data->blocks * mrq->data->blksz >
			host->max_req_size);


		mrq->cmd->data = mrq->data;
		mrq->data->error = 0;
		mrq->data->mrq = mrq;
		if (mrq->stop) {
			mrq->data->stop = mrq->stop;
			mrq->stop->error = 0;
			mrq->stop->mrq = mrq;
		}
	}

	return 0;
}

#define GZIP_MAX_PAGE_CNT 33
static int sdp_unzip_test_request_mmc(struct mmc_host *host, u32 buf_addr, u32 blk_addr, u32 blk_count, int is_write)
{
	struct mmc_card *card = host->card;

	static struct scatterlist sg[GZIP_MAX_PAGE_CNT];
	struct mmc_request mrq = {0};
	struct mmc_command sbc = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data mmcdata = {0};

	int ret, i, sg_num = 0;

	u32 status[4];

	sg_num = TO_ALIGNED(blk_count*512, PAGE_SIZE) / PAGE_SIZE;

	sg_init_table(sg, sg_num);
	for(i = 0; i < sg_num-1; i++) {
		sg_set_buf(sg+i, (void *)buf_addr+(PAGE_SIZE*i), PAGE_SIZE);
	}
	if((blk_count*512)%PAGE_SIZE == 0)
		sg_set_buf(sg+i, (void *)buf_addr+(PAGE_SIZE*i), PAGE_SIZE);
	else
		sg_set_buf(sg+i, (void *)buf_addr+(PAGE_SIZE*i), (blk_count*512)%PAGE_SIZE);


	if(mmc_host_cmd23(card->host) && (card->ext_csd.rev >= 6) && !(card->quirks & MMC_QUIRK_BLK_NO_CMD23))
		mrq.sbc = &sbc;
	mrq.cmd = &cmd;
	mrq.data = &mmcdata;
	mrq.stop = &stop;

	//mmch_dbg_prepare_mrq(card, &mrq, mmch_dbg_get_capacity(card)-0x2000/*4MB*/, 0x2000, true);
	mmch_dbg_prepare_mrq(card, &mrq, blk_addr, blk_count, is_write);

	mmcdata.sg = sg;
	mmcdata.sg_len = sg_num;

	mmc_claim_host(host);
	mmc_wait_for_req(host, &mrq);

	do {
		udelay(10);
		mmc_send_status(host->card, status);
	} while( !(status[0] & R1_READY_FOR_DATA) || (R1_CURRENT_STATE(status[0]) == R1_STATE_PRG) );

	mmc_release_host(host);

	ret = cmd.error | (mmcdata.error <<16);

	return ret;
}

#endif



void print_comp_request(struct comp_request *req)
{
	pr_info("Req next_in 0x%p, avail_in %d, next_out 0x%p, avail_out %d\n", req->next_in, req->avail_in, req->next_out, req->avail_out);
}

//#define ZLIB_DEFLATE
extern int sdp_unzip_sw_zlib_comp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_zlib_decomp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_gzip_comp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_gzip_decomp(u8 *input, u32 ilen, u8 *output, u32 olen);
extern int sdp_unzip_sw_aes_ctr128_crypt(const void *key, int key_len, const void *iv, const char *clear_text, char *cipher_text, size_t size);
extern int sdp_unzip_sw_sha256_digest(const u8 *input, u32 ilen, u8 *out_digest);


enum test_result {
	PASS = 0,
	ERR_HASH,
	ERR_AES,
	ERR_GZIP,
	ERR_MISS_MATCH,
	ERR_SIZE,
	ERR_HASH_MISS_MATCH,
};

int sdp_unzip_test(void* data)
{
	struct sdp_unzip_test_thread *thread = data;
	const u32 BUF_SIZE = N_PAGES << PAGE_SHIFT;

	void *src_buf;		// Source buffer
	void *comp_buf;		// Destinaion buffer
	u32 src_size;	// Size buffer


	u32 src_paddr;		// Physcal address of source buffer
	u32 dest_paddr;		// Physcal address of destination buffer

	void *decomp_buf;	// Decompress by HW
	struct page *decomp_pages[N_PAGES];

	void *encrypt_buf;
	u8 *bitmap;
	void *gzip_input;

	u8 sha256_ref[SHA256_DIGEST_SIZE];
	u8 sha256_input[SHA256_DIGEST_SIZE];
	u8 sha256_output[SHA256_DIGEST_SIZE];

	u32 comp_len;
	u32 comp_padded_len;

	int res, case_res, err_injected;
	int i, total_err = 0, loop;
	u32 flags = 0;

	struct sdp_unzip_auth_t auth;

	char prefix[30];

	u64 total_src = 0, total_comp = 0;
	unsigned long next_print = jiffies + msecs_to_jiffies(N_PRINT_TIME_MS);

	unsigned long long time_start, time_end, total_time_us = 0, total_output_bytes = 0, total_input_bytes = 0;

	snprintf(prefix, 30, "GZIPTEST%02d", thread->id);

	/* 2. Memory allocation */
	// SRC
	src_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (src_buf == NULL) {
		printk("SRC alloc - failed\n");
		return -ENOMEM;
	}
	src_paddr = (u32)virt_to_phys(src_buf);

	// COMP
	comp_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (comp_buf == NULL) {
		printk("DEST alloc - failed\n");
		return -ENOMEM;
	}
	dest_paddr = (u32)virt_to_phys(comp_buf);

	// ENCRYPT
	encrypt_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (encrypt_buf == NULL) {
		printk("ENCRYPT alloc - failed\n");
		return -ENOMEM;
	}

	// DECOMP
	decomp_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (decomp_buf == NULL) {
		printk("DECOMP alloc - failed\n");
		return -ENOMEM;
	}
	for(i = 0; i < N_PAGES; i++) {
		decomp_pages[i] = virt_to_page(decomp_buf + (PAGE_SIZE * i));
	}

	// BITMAP
	bitmap = kmalloc(BUF_SIZE/8, GFP_KERNEL);
	if (bitmap == NULL) {
		printk("BITMAP alloc - failed\n");
		return -ENOMEM;
	}

#ifdef USE_GZIP_OTF_MMC_READ
#define MMC_DEV_PATH	"/dev/mmcblk0p4"
	if(1) {
		struct block_device *bdev;

		//wait_for_completion(&mmc_rescan_work);

		bdev = blkdev_get_by_path(MMC_DEV_PATH, FMODE_READ|FMODE_WRITE, thread);
		if (IS_ERR(bdev)) {
			pr_err("%s: can't get blkdev(%s) return %ld\n", prefix, MMC_DEV_PATH, PTR_ERR(bdev));
		} else {
			/* XXX : temp... */
			struct mmc_queue *mq = (struct mmc_queue *) (bdev_get_queue(bdev)->queuedata);
			mmc = mq->card->host;
		}
	}
#endif



	for(loop = 0; (thread->id < nr_thread) && (!N_LOOP || (loop < N_LOOP)); loop++) {
		flags = 0;
		case_res = PASS;
		err_injected = 0;
		snprintf(prefix, 30, "GZIPTEST%02d #%d", thread->id, loop);
		memset(&auth, 0x0, sizeof(auth));

		if(thread->use_zlib) {
			flags |= GZIP_FLAG_ZLIB_FORMAT;
		}

		/* Make test case input data */
		if(i == 0) {
			src_size = ARRAY_SIZE(sample_text) - 1;
			memcpy(src_buf, sample_text, src_size);

		} else if(false) {/* filled phyaddr */
			int pos = 0;

			get_random_bytes(&src_size, sizeof(src_size));
			src_size = TO_ALIGNED((src_size%BUF_SIZE)+1, thread->src_align);

			if(thread->src_size > 0) {
				src_size = thread->src_size;
			}

			for(pos = 0;  pos < src_size; pos+=4) {
				*((unsigned int *)(src_buf + pos)) = src_paddr + pos;
			}

		}else {/* fully random source!!!! */
			int pos = 0;

			get_random_bytes(&src_size, sizeof(src_size));
			src_size = TO_ALIGNED((src_size%BUF_SIZE)+1, thread->src_align);

			if(thread->src_size > 0) {
				src_size = thread->src_size;
			}

			get_random_bytes(src_buf, src_size);


			get_random_bytes(bitmap, BUF_SIZE/8);
			/* random inject 0x0 */
			for(pos = 0; pos < src_size; pos++) {
				if(!(bitmap[pos/8] & (0x1<<(pos%8)))) {
					((u8 *)src_buf)[pos] = 0x0;
				}
			}

			get_random_bytes(bitmap, BUF_SIZE/8);
			/* random inject 0xFF */
			for(pos = 0; pos < src_size; pos++) {
				if(!(bitmap[pos/8] & (0x1<<(pos%8)))) {
					((u8 *)src_buf)[pos] = 0xFF;
				}
			}

			get_random_bytes(bitmap, BUF_SIZE/8);
			/* random inject 0xAA */
			for(pos = 0; pos < src_size; pos++) {
				if(!(bitmap[pos/8] & (0x1<<(pos%8)))) {
					((u8 *)src_buf)[pos] = 0xAA;
				}
			}
		}

		/* Start test case */
//		pr_info("%s: GZIP Compress start src size %d\n", src_size);


		if(flags&GZIP_FLAG_ZLIB_FORMAT) {
			res = sdp_unzip_sw_zlib_comp(src_buf, src_size, comp_buf, BUF_SIZE);

		} else {
			res = sdp_unzip_sw_gzip_comp(src_buf, src_size, comp_buf, BUF_SIZE);
		}
		if(res <= 0) {
			pr_info("%s: %s Compressed error %d\n", prefix, (flags&GZIP_FLAG_ZLIB_FORMAT)?"zlib":"gzip", res);
			continue;
			//return -1;
		}
		comp_len = res;
		gzip_input = comp_buf;
		//pr_info("%s: Compressed done src size %d -> comp size %dbytes\n", prefix, src_size, comp_len);


//		if(comp_len >= src_size) {
//			pr_info(%s: Compressed too Big! skip sample! src %dbytes, comp %dbytes\n", prefix, src_size, comp_len);
//			continue;
//		}


		comp_padded_len = TO_ALIGNED(comp_len, thread->comp_align);

		if(comp_len < comp_padded_len) {
			memset(comp_buf + comp_len, 0x0, comp_padded_len - comp_len);
		}



#if defined(USE_GZIP_AUTH) | defined(USE_GZIP_DECRYPT)
		/* padding 0x0 */
		comp_padded_len = TO_ALIGNED(comp_padded_len, thread->sha_msg_align);
		if(comp_len < comp_padded_len) {
			memset(comp_buf + comp_len, 0x0, comp_padded_len - comp_len);
		}
#endif

#ifdef USE_GZIP_DECRYPT
		res = sdp_unzip_sw_aes_ctr128_crypt(sample_key, 16, sample_iv, comp_buf, encrypt_buf, comp_padded_len);
		gzip_input = encrypt_buf;

		auth.aes_ctr_iv = (u32 *)sample_iv;
		auth.aes_user_key = (u32 *)sample_key;
#endif

		res = sdp_unzip_sw_sha256_digest(gzip_input, comp_padded_len, sha256_ref);
		if(res < 0) {
			pr_err("%s: SHA256 Digest failed! skip sample! src %dbytes, comp %dbytes\n", prefix, src_size, comp_len);
			continue;
		}
#ifdef USE_GZIP_AUTH
		memcpy(sha256_input, sha256_ref, SHA256_DIGEST_SIZE);
		memset(sha256_output, 0x0, SHA256_DIGEST_SIZE);
//		auth.sha256_digest = (u32 *)&sha256_input;
		auth.sha256_digest_out = (u32 *)&sha256_output;
		auth.sha256_length = comp_padded_len;
#endif


		if(err_inject_mod) {
			int inject_random = 0;

			get_random_bytes(&inject_random, sizeof(inject_random));
			if((inject_random%err_inject_mod) == 0) {
				int idx = inject_random%comp_len;
				u8 *err_byte = (u8 *)gzip_input;
				pr_err("%s: error inject! data[0x%x] = 0x%02x -> 0x%02x\n", prefix, idx, err_byte[idx], err_byte[idx]^0xFF);
				err_byte[idx] ^= 0xFF;
				err_injected = 1;
			} else if(auth.sha256_digest && ((inject_random%err_inject_mod) == 1)) {
				int idx = (inject_random%comp_len)%SHA256_DIGEST_SIZE;
				u8 *err_byte = (u8 *)auth.sha256_digest;
				pr_err("%s: error inject! sha256_digest[0x%x] = 0x%02x -> 0x%02x\n", prefix, idx, err_byte[idx], err_byte[idx]^0xFF);
				err_byte[idx] ^= 0xFF;
				err_injected = 2;
			}
		}


//		printk(KERN_INFO "GZIP SW Input data validate start\n");

		if(auth.sha256_digest) {
			u8 sha256_cal[SHA256_DIGEST_SIZE];
			sdp_unzip_sw_sha256_digest(gzip_input, comp_padded_len, sha256_cal);
			if(memcmp(sha256_ref, sha256_cal, SHA256_DIGEST_SIZE)) {
				pr_err("%s: SW SHA256 validate error! src %dbytes, comp %dbytes, comp_padded %dbytes\n", prefix,
					src_size, comp_len, comp_padded_len);
			}
		}

		if(auth.aes_ctr_iv) {
			res = sdp_unzip_sw_aes_ctr128_crypt(sample_key, 16, sample_iv, encrypt_buf, comp_buf, comp_padded_len);
			if(res < 0)  {
				pr_err("%s: SW AES-CTR Decrypt error! src %dbytes, comp %dbytes\n", prefix, src_size, comp_len);
			}
		}

		total_input_bytes += comp_len;
		total_output_bytes += src_size;
		time_start = sched_clock();
		if(flags & GZIP_FLAG_ZLIB_FORMAT) {
			res = sdp_unzip_sw_zlib_decomp(comp_buf, comp_len, decomp_buf, BUF_SIZE);
		} else {
			res = sdp_unzip_sw_gzip_decomp(comp_buf, comp_len, decomp_buf, BUF_SIZE);
		}
		time_end = sched_clock();
		total_time_us += div64_u64(time_end - time_start, 1000UL);

		if(res < 0)  {
			pr_err("%s: SW Decomp(%s) error! src %dbytes, comp %dbytes\n", prefix,
				(flags&GZIP_FLAG_ZLIB_FORMAT)?"zlib":"gzip", src_size, comp_len);
		} else {
			if(res != src_size) {
				pr_err("%s: SW decomp size error! src %dbytes, comp %dbytes, decomp %dbytes\n", prefix, src_size, comp_len, res);
			}else if(memcmp(src_buf, decomp_buf, src_size)) {
				pr_err("%s: SW decomp data validate error! src %dbytes, comp %dbytes\n", prefix, src_size, comp_len);
			}
		}
//		printk(KERN_INFO "GZIP SW Input data validate done\n");
//		printk(KERN_INFO "%d:GZIP Decompressed info #%d ret %d, src 0x%05x, comp 0x%05x, padded 0x%05x\n", thread->id, loop, res, src_size, comp_len, comp_padded_len);
//		pr_info("%s: HW Decompress start\n", prefix);

		//goto valid;//skip HW Decomp

		/* clear decomp buf */
		memset(decomp_buf, 0x0, BUF_SIZE);
#ifdef USE_GZIP_OTF_MMC_READ
		if(mmc) {
			u32 blk_count = TO_ALIGNED(comp_padded_len, 512)/512;
			//u32 blk_addr = mmch_dbg_get_capacity(mmc->card) - blk_count;
			u32 blk_addr = 0x40000;
			dma_addr_t ibuff_phys;
			struct sdp_unzip_req uzreq;
			void *tmpbuf;
			static int naddr = 0;

			blk_addr += (naddr * 4096);
			if(naddr > 10) naddr = 0;
			naddr++;

			//pr_info("%s: MMC Write start size 0x%X bytes blk_addr=%08X\n", prefix, blk_count * 512, blk_addr);
			res = sdp_unzip_test_request_mmc(mmc, (u32)gzip_input, blk_addr, blk_count, true);		//Write Source Data

			flags |= GZIP_FLAG_OTF_MMCREAD;

			memset(gzip_input, 0, comp_padded_len);	//clear input buffer
			dmac_flush_range(gzip_input, gzip_input + comp_padded_len);

			/* Prepare input buffer */

			memset(&uzreq, 0x0, sizeof(uzreq));
			uzreq = (struct sdp_unzip_req) {
				.vaddr = gzip_input,
				.paddr = (u32)virt_to_phys(gzip_input),
				.size  = comp_padded_len,
				.flags = flags,
				.auth = auth,
			};

			flags |= GZIP_FLAG_ENABLE_AUTH;
			
			sdp_unzip_decompress_async(&uzreq, 0, decomp_pages, TO_ALIGNED(src_size, PAGE_SIZE)/PAGE_SIZE, NULL, NULL, true);

			res = sdp_unzip_test_request_mmc(mmc, (u32)gzip_input, blk_addr, blk_count, false);		//Read Source Data
			if (!res) {
				/* Kick decompressor to start right now */
				sdp_unzip_update_endpointer(&uzreq);

				/* Wait and drop lock */
				res = sdp_unzip_decompress_wait(&uzreq);
			} else {
				pr_err("Unzip Error!!!!\n");
				return;
			}
		}
#else /*USE_GZIP_OTF_MMC_READ*/

	//time_start, time_end, total_time_us = 0, total_output_bytes = 0;
	if(1) {
		struct sdp_unzip_desc *uzdesc;
		struct scatterlist in_sgl[1];
		struct scatterlist out_sgl[N_PAGES];

#ifdef USE_GZIP_AUTH
		flags |= GZIP_FLAG_ENABLE_AUTH;
#endif

		uzdesc = sdp_unzip_alloc_descriptor(flags);

#ifdef CONFIG_SDP_UNZIP_AUTH
		if(flags & GZIP_FLAG_ENABLE_AUTH) {
			uzdesc->auth = auth;
		}
#endif

		sg_init_one(in_sgl, gzip_input, comp_padded_len);
		sg_init_table(out_sgl, N_PAGES);
		for(i = 0; i < N_PAGES; i++) {
			sg_set_page(&out_sgl[i], decomp_pages[i], PAGE_SIZE, 0x0);
		}

		//total_output_bytes += src_size;
		//time_start = sched_clock();
		sdp_unzip_decompress_sync(uzdesc, in_sgl, out_sgl, true);
		//time_end = sched_clock();
		//total_time_us += div64_u64(time_end - time_start, 1000UL);
		sdp_unzip_free_descriptor(uzdesc);


#ifdef USE_GZIP_AUTH
		memcpy(auth.sha256_digest_out, uzdesc->auth.sha256_digest_out, SHA256_DIGEST_SIZE);
#endif
	}


#endif/*USE_GZIP_OTF_MMC_READ*/
//		pr_info("%s: HW Decompress done\n", prefix);

goto valid;//clear warning...
valid:
		if(err_injected == 1) {
			if(res == src_size) {
				if(case_res == PASS) total_err++;
				pr_err("%s: Error injected Fail!! src %dbytes, comp %dbytes, decomp %dbytes\n", prefix, src_size, comp_len, res);
			} else {
				pr_err("%s: Error injected Pass - data\n", prefix);
			}
		} else if(err_injected == 2) {
			if(auth.sha256_digest) {
				if (auth.sha256_digest_out && !memcmp(auth.sha256_digest_out, auth.sha256_digest, SHA256_DIGEST_SIZE)) {
					if(case_res == PASS) total_err++;
					pr_err("%s: Error injected Fail!! SW Hash compare result is OK!! src %dbytes, comp %dbytes, decomp %dbytes\n", prefix, src_size, comp_len, auth.sha256_result);
				} else if(auth.sha256_result == GZIP_HASH_OK) {
					if(case_res == PASS) total_err++;
					pr_err("%s: Error injected Fail!! HW Hash result is OK!! src %dbytes, comp %dbytes, decomp %dbytes\n", prefix, src_size, comp_len, auth.sha256_result);
				} else {
					pr_err("%s: Error injected Pass - hash\n", prefix);
				}
			}
		} else if(res < 0) {
				if(case_res == PASS) total_err++;
				case_res = -ERR_GZIP;

				printk(KERN_INFO "%d:GZIP Decompressed error #%d err %d, src %7d, comp %7d\n", thread->id, loop, res, src_size, comp_len);
				printk(KERN_INFO "%d:GZIP Decompressed error #%d err %d, src 0x%05x, comp 0x%05x, padded 0x%05x\n", thread->id, loop, res, src_size, comp_len, comp_padded_len);
		} else {
			if (res != src_size) {
				if(case_res == PASS) total_err++;
				case_res = -ERR_SIZE;
				printk(KERN_ERR "%d:GZIP size error: #%d, src %dbytes, comp %dbytes, decomp %dbytes\n", thread->id, loop, src_size, comp_len, res);
			} else if (memcmp(decomp_buf, src_buf, src_size)) {
				const u32 CMP_CHUNK = 0x10;
				u32 cur = 0, dump_limit = 50;

				if(case_res == PASS) total_err++;
				case_res = -ERR_MISS_MATCH;
				printk(KERN_ERR "%d:GZIP validate error: #%d, src %dbytes, comp %dbytes\n", thread->id, loop, src_size, comp_len);

				/* missmatch part dump */
				for(cur = 0; cur < src_size; cur += CMP_CHUNK) {
					int chunk = min(src_size-cur, CMP_CHUNK);

					if(memcmp(decomp_buf + cur, src_buf + cur, chunk)) {
						print_hex_dump(KERN_ERR, "source: ", DUMP_PREFIX_ADDRESS, 16, 1, src_buf + cur, chunk, true);
						print_hex_dump(KERN_ERR, "decomp: ", DUMP_PREFIX_ADDRESS, 16, 1, decomp_buf + cur, chunk, true);

						dump_limit--;
						if(dump_limit == 0) {
							printk(KERN_ERR "%d:GZIP end more...\n", thread->id);
							break;
						}
					}
				}
			}

			if(auth.sha256_digest) {
				if(auth.sha256_result != GZIP_HASH_OK) {
					if(case_res == PASS) total_err++;
					case_res = -ERR_HASH_MISS_MATCH;
					printk(KERN_ERR "%d:GZIP SHA256 HW Compare Hash missmatch: #%d, src %dbytes, comp %dbytes, sha256_result %d\n", thread->id, loop, src_size, comp_len, auth.sha256_result);
				}

				if (auth.sha256_digest_out && memcmp(auth.sha256_digest_out, auth.sha256_digest, SHA256_DIGEST_SIZE)) {
					if(case_res == PASS) total_err++;
					case_res = -ERR_HASH_MISS_MATCH;
					printk(KERN_ERR "%d:GZIP SHA256 SW Compare Hash missmatch: #%d, src %dbytes, comp %dbytes, HW Result %d\n", thread->id, loop, src_size, comp_len, auth.sha256_result);
				}
			}
		}



		/* dump data */
		if(!err_injected && case_res < 0) {
			if(auth.sha256_digest) {
				print_hex_dump(KERN_INFO, "SHA256: ", DUMP_PREFIX_ADDRESS, 16, 1, auth.sha256_digest, SHA256_DIGEST_SIZE, true);
			}

			if(auth.sha256_digest_out) {
				print_hex_dump(KERN_INFO, "SHA256OUT: ", DUMP_PREFIX_ADDRESS, 16, 1, auth.sha256_digest_out, SHA256_DIGEST_SIZE, true);
			}

			if(auth.aes_ctr_iv) {
				print_hex_dump(KERN_INFO, "KEY: ", DUMP_PREFIX_ADDRESS, 16, 1, auth.aes_user_key, 16, true);
				print_hex_dump(KERN_INFO, "IV: ", DUMP_PREFIX_ADDRESS, 16, 1, auth.aes_ctr_iv, 16, true);
			}


			if(0) {
				const char *path = "/tmp";
				char name[100] = "";
				struct file* file = NULL;
				unsigned long now = jiffies;

				snprintf(name, ARRAY_SIZE(name), "%s/%08lu_dump_src%06d(0x%05x)_gzip%06d(0x%05x)_padded%06d(0x%05x)",
					path, now, src_size, src_size, comp_len, comp_len, comp_padded_len, comp_padded_len);
				pr_info("save to %s\n", name);

				file = file_open(name, O_WRONLY|O_CREAT, 0644);
				file_write(file, 0, src_buf, src_size);
				file_sync(file);
				file_close(file);

				snprintf(name, ARRAY_SIZE(name), "%s/%08lu_dump_src%06d(0x%05x)_gzip%06d(0x%05x)_padded%06d(0x%05x).gz",
					path, now, src_size, src_size, comp_len, comp_len, comp_padded_len, comp_padded_len);
				pr_info("save to %s\n", name);

				file = file_open(name, O_WRONLY|O_CREAT, 0644);
				file_write(file, 0, comp_buf, comp_len);
				file_sync(file);
				file_close(file);

				if(auth.aes_ctr_iv) {
					snprintf(name, ARRAY_SIZE(name), "%s/%08lu_dump_src%06d(0x%05x)_gzip%06d(0x%05x)_padded%06d(0x%05x).gz.aes",
						path, now, src_size, src_size, comp_len, comp_len, comp_padded_len, comp_padded_len);
					pr_info("save to %s\n", name);

					file = file_open(name, O_WRONLY|O_CREAT, 0644);
					file_write(file, 0, encrypt_buf, comp_padded_len);
					file_sync(file);
					file_close(file);
				}

			} else {
				if(comp_len <= 0x400) {
					print_hex_dump(KERN_ERR, "comp  : ", DUMP_PREFIX_ADDRESS, 16, 1, gzip_input, comp_padded_len, true);
				} else {
					print_hex_dump(KERN_ERR, "comp 1: ", DUMP_PREFIX_ADDRESS, 16, 1, gzip_input, 0x200, true);
					print_hex_dump(KERN_ERR, "comp 2: ", DUMP_PREFIX_ADDRESS, 16, 1, gzip_input+comp_padded_len-0x200, 0x200, true);
				}
			}
		} else {
			total_src += src_size;
			total_comp += comp_len;
		}


		if(time_after_eq(jiffies, next_print)) {
			unsigned long long output_speed = div64_u64(total_output_bytes, total_time_us);
			unsigned long long input_speed = div64_u64(total_input_bytes, total_time_us);

			next_print = jiffies + msecs_to_jiffies(N_PRINT_TIME_MS);
			pr_info("%s: test running... total src %llubytes, comp %llubytes, error#%d, speed in %lluMB/s, out %lluMB/s\n",
				prefix, total_src, total_comp, total_err, input_speed, output_speed);
		}
	}

	pr_info("%s: tested total #%d test all done(error#%d)\n", prefix, loop, total_err);
	return 0;
}


int sdp_unzip_test_add_thread(struct sdp_unzip_test_t *unziptest)
{
	struct sdp_unzip_test_thread *thread = kzalloc(sizeof(struct sdp_unzip_test_thread), GFP_KERNEL);

	thread->id = nr_thread++;

	thread->src_size = unziptest->src_size;

#ifdef USE_GZIP_ZLIB
	thread->use_zlib = thread->id&0x1?false:unziptest->use_zlib;
#endif
	thread->src_align = unziptest->src_align;
	thread->comp_align = unziptest->comp_align;
	thread->sha_msg_align = unziptest->sha_msg_align;

	pr_info("sdp_unzip_test: start new test thread!(id%d)"
			" use %s, aligned(src %d, comp %d, sha_msg %d)\n",
			thread->id, thread->use_zlib?"zlib":"gzip", thread->src_align, thread->comp_align, thread->sha_msg_align);

	thread->task = kthread_run(sdp_unzip_test, thread, "unzip_test%d", thread->id);
	if (IS_ERR(thread->task)) {
		nr_thread--;
		pr_warning("sdp_unzip_test_init: Failed to run thread(id %d)\n", thread->id);
		return -EBUSY;
	}
	return 0;
}




#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

#define KER_BUF_LEN	10
#define KER_STR_LEN	200
struct dentry *dirret, *fileret;

/* read file operation */
static int sdp_unzip_test_nr_thread_get(void *data, u64 *val)
{
	//struct sdp_unzip_test_t *unziptest = data;
	*val = nr_thread;
	return 0;
}

/* write file operation */
static int sdp_unzip_test_nr_thread_set(void *data, u64 val)
{
	struct sdp_unzip_test_t *unziptest = data;
	int ret;

	if(nr_thread > val) {
		nr_thread = val;
	} else {
		while(nr_thread < val) {
			ret = sdp_unzip_test_add_thread(unziptest);
			if(ret < 0) {
				pr_err("sdp_unzip_test_add_thread retrun error %d!\n", ret);
				return ret;
			}
		}
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(sdp_unzip_test_nr_thread_fops, sdp_unzip_test_nr_thread_get, sdp_unzip_test_nr_thread_set,
	"%llu\n");


static int __init sdp_unzip_test_init_debugfs(struct sdp_unzip_test_t *unziptest)
{
	//int filevalue;
	/* create a directory by the name dell in /sys/kernel/debugfs */
	dirret = debugfs_create_dir("sdp_unzip_test", NULL);

	/* create a file in the above directory
	This requires read and write file operations */
	fileret = debugfs_create_file("nr_thread", 0644, dirret, unziptest, &sdp_unzip_test_nr_thread_fops);

	debugfs_create_x32("src_size", 0644, dirret, &unziptest->src_size);
	debugfs_create_u32("src_align", 0644, dirret, &unziptest->src_align);
	debugfs_create_u32("comp_align", 0644, dirret, &unziptest->comp_align);
	debugfs_create_u32("sha_msg_align", 0644, dirret, &unziptest->sha_msg_align);
	debugfs_create_bool("use_zlib", 0644, dirret, &unziptest->use_zlib);

#if 0
	/* create a file which takes in a int(64) value */
	u64int = debugfs_create_u64("number", 0644, dirret, &intvalue);
	if (!u64int) {
		printk("error creating int file");
		return (-ENODEV);
	}
	/* takes a hex decimal value */
	u64hex = debugfs_create_x64("hexnum", 0644, dirret, &hexvalue );
	if (!u64hex) {
		printk("error creating hex file");
		return (-ENODEV);
	}
#endif

	return 0;
}
#endif

int __init sdp_unzip_test_init(void)
{
	int ret, i = 0;
	struct sdp_unzip_test_t *unziptest = kzalloc(sizeof(struct sdp_unzip_test_t), GFP_KERNEL);

#ifdef USE_GZIP_ZLIB
	unziptest->use_zlib = true;
#endif
	unziptest->src_align = DEFAULT_SRC_ALIGN;
	unziptest->comp_align = DEFAULT_GZIP_ALIGN;
	unziptest->sha_msg_align = DEFAULT_SHA_MSG_ALIGN;

#ifdef CONFIG_DEBUG_FS
	ret = sdp_unzip_test_init_debugfs(unziptest);
#endif



	for(i = 0; i < N_THERADS; i++) {
		ret = sdp_unzip_test_add_thread(unziptest);
		if(ret < 0) {
			pr_err("sdp_unzip_test_add_thread retrun error %d!\n", ret);
		}
	}

	return 0;
}

late_initcall(sdp_unzip_test_init);

void __exit sdp_unzip_test_exit(void)
{
	printk(KERN_INFO "Exit module\n");

}
module_exit(sdp_unzip_test_exit);
MODULE_LICENSE("GPL");
