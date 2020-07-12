/*
 *
 * btpcm_ibiza.c
 *
 *
 *
 * Copyright (C) 2013 Broadcom Corporation.
 *
 *
 *
 * This software is licensed under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation (the "GPL"), and may
 * be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL for more details.
 *
 *
 * A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php
 * or by writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA
 *
 *
 */

#define BTPCM_NB_STREAM_MAX 1

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include <linux/timer.h>
#include <linux/jiffies.h>

#include <linux/proc_fs.h>  /* Necessary because we use proc fs */
#include <linux/seq_file.h> /* for seq_file */
#include <asm/byteorder.h>  /* For Little/Big Endian */

#include <btpcm.h>
#include <btpcm_hrtimer.h>

/*
 * Definitions
 */

/* For 16bits per samples, Stereo, the PCM Frame size is 4 (2 * 2) */
#define BTPCM_FRAME_SIZE        (BTPCM_SAMPLE_16BIT_SIZE * BTPCM_SAMPLE_STEREO_SIZE)

#ifndef BTPCM_HW_PCM_SAMPLE_SIZE
/* HW PCM Sample size is 4 bytes (Little Endian, MSB unused (24 bits)) */
#define BTPCM_HW_PCM_SAMPLE_SIZE                4
#endif

#if (BTPCM_HW_PCM_SAMPLE_SIZE != 2) && (BTPCM_HW_PCM_SAMPLE_SIZE != 4)
#error "BTPCM_HW_PCM_SAMPLE_SIZE must be either 2 or 4 bytes"
#endif

#ifdef BTPCM_IBIZA_TONE

/* For simulation (tone) use two 20 ms buffer instead of the HW buffer */
#define BTPCM_IBIZA_PCM_BUF_SIZE            (128 * 12 * BTPCM_HW_PCM_SAMPLE_SIZE)
static uint8_t btpcm_ibiza_tone_buf[BTPCM_IBIZA_PCM_BUF_SIZE * 2];
static int btpcm_ibiza_tone_write_offset;
#define __iomem
#define IOREAD8(p) (*(p))       /* Pointer can be used to access the Tone buffer */

#else /* BTPCM_IBIZA_TONE */

#include <linux/memory.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <asm/io.h> /* for ioremap/iounmap */

/* Start address of the Left PCM buffer */
#ifndef BTPCM_IBIZA_PCM_BUF_LEFT_ADDR
#define BTPCM_IBIZA_PCM_BUF_LEFT_ADDR       0x33420000
#endif

#if 0
/* Start address of the Right PCM buffer */
#ifndef BTPCM_IBIZA_PCM_BUF_RIGHT_ADDR
#define BTPCM_IBIZA_PCM_BUF_RIGHT_ADDR      0xE0EE1000
#endif
#endif

/* Size of each buffer */
#ifndef BTPCM_IBIZA_PCM_BUF_SIZE
#define BTPCM_IBIZA_PCM_BUF_SIZE            0x15000     /* 84K Bytes */
#endif

/* Address of the Left PCM Write */
#ifndef BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR
#define BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR     0x334f0b14
#endif

/* Address of the Enable Mark */
#ifndef BTPCM_IBIZA_PCM_ENABLE_MARK
#define BTPCM_IBIZA_PCM_ENABLE_MARK     0x334f0b66 //GolfP
#endif

/* IO Function must be used to access the remapped memory */
/* Note that for ARM architecture, it's just a memory read Macro*/
#define IOREAD8(p) ioread8(p)
#define IOREAD16(p) ioread16(p)

#ifdef __LITTLE_ENDIAN
#define IOREAD32(p) ioread32(p)     /* Little Endian */
#else
#define IOREAD32(p) ioread32be(p)   /* Big Endian */
#endif

#endif /* !BTPCM_IBIZA_TONE */

#define BTPCM_IBIZA_PROC_NAME       "driver/btpcm-ibiza"

/* Commands accepted by /proc/driver/btpcm-ibiza */
#define BTPCM_IBIZA_CMD_DBG     '0'
#define BTPCM_IBIZA_CMD_REOPEN  '1'

/* Ibiza Channel Control Block */
struct btpcm_ibiza_ccb
{
    int opened;
    void (*callback) (int stream, void *p_opaque, void *p_buf, int nb_pcm_frames);
    void *p_opaque;
    int frequency;
    int nb_channel;
    int bits_per_sample;
    struct btpcm_hrtimer *hr_timer;
    int nb_frames; /* Number of PCM frames */
    int nb_packets; /* Number of packet */
    atomic_t started;
    int left_read_offset;
    void __iomem *p_left_io_pcm;   /* Remapped pointer to Left PCM Buffer */
    void __iomem *p_left_io_ptr;   /* Remapped pointer to Left Write Pointer */
    void __iomem *p_left_io_enablemark;   /* Remapped pointer to Enable Mark Pointer */
    void *p_buf; /* PCM buffer (used to construct an interleaved PCM buffer from the two HW Mono buffers) */
#ifdef BTPCM_IBIZA_DEBUG
    int timer_duration; /* timer duration in jiffies (for debug)  */
    int jiffies; /* debug */
#endif
#ifndef BTPCM_IBIZA_TONE
    uint ibiza_pcm_buf_left_phy_addr;
    uint ibiza_pcm_buf_size;
    uint ibiza_pcm_left_write_phy_addr;
    uint ibiza_enable_mark_addr;
    void * p_enable_mark_pointer_before;
#endif /* ! BTPCM_IBIZA_TONE */
};

/* Ibiza Control Block */
struct _btpcm_ibiza_cb
{
    struct btpcm_ibiza_ccb ccb[BTPCM_NB_STREAM_MAX];
};

/*
 * Global variables
 */
#ifdef BTPCM_IBIZA_TONE
/* Two sinus waves (one for left and one for right) */
static const short btpcm_ibiza_sinwaves[2][64] =
{
        {
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488,
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488
        },
        {
         0,    244,    488,    722,    957,  1173,  1389,   1578,
      1768,   1923,   2079,   2194,   2310,  2381,  2452,   2476,
      2500,   2476,   2452,   2381,   2310,  2194,  2079,   1923,
      1768,   1578,   1389,   1173,    957,   722,   488,    244,
         0,   -244,   -488,   -722,   -957, -1173, -1389,  -1578,
     -1768,  -1923,  -2079,  -2194,  -2310, -2381, -2452,  -2476,
     -2500,  -2476,  -2452,  -2381,  -2310, -2194, -2079,  -1923,
     -1768,  -1578,  -1389,  -1173,   -957,  -722,  -488,   -244
        }
};

static uint btpcm_ibiza_param_test = 0xE0ECC000;
module_param(btpcm_ibiza_param_test, uint, 0664);
MODULE_PARM_DESC(btpcm_ibiza_param_test, "BTPCM Ibiza Test Parameter");

#else /* BTPCM_IBIZA_TONE */

#define BTPCM_IBIZA_PARAM_PERM  (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH)

/* Ibiza's Platform Default PCM Buffer Left Physical Address */
static uint ibiza_pcm_buf_left_phy_addr = BTPCM_IBIZA_PCM_BUF_LEFT_ADDR;
module_param(ibiza_pcm_buf_left_phy_addr, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_buf_left_phy_addr, "Ibiza PCM Buffer Left Physical Address");

/* Ibiza's Platform Default PCM Buffer Left/Right Size  */
static uint ibiza_pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
module_param(ibiza_pcm_buf_size, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_buf_size, "Ibiza PCM Buffer Size (Left or Right)");

/* Ibiza's Platform Default PCM Buffer Left Write Pointer Address */
static uint ibiza_pcm_left_write_phy_addr = BTPCM_IBIZA_PCM_LEFT_WRITE_ADDR;
module_param(ibiza_pcm_left_write_phy_addr, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_pcm_left_write_phy_addr, "Ibiza PCM Buffer Left Write Pointer Physical Address");

/* Ibiza's Platform Default Enable Mark Address*/
static uint ibiza_enable_mark_addr = BTPCM_IBIZA_PCM_ENABLE_MARK;
module_param(ibiza_enable_mark_addr, uint, BTPCM_IBIZA_PARAM_PERM);
MODULE_PARM_DESC(ibiza_enable_mark_addr, "Ibiza Enable Mark Physical Address");

static uint ibiza_pcm_access_point_address;
static uint ibiza_pcm_m_address;
#endif /* !BTPCM_IBIZA_TONE */

struct _btpcm_ibiza_cb btpcm_ibiza_cb; /* BTPCM control block */

/*
 * Local functions
 */
#ifdef BTPCM_IBIZA_TONE
static void btpcm_ibiza_tone_init(void);
static void btpcm_ibiza_tone_simulate_write(int nb_pcm_samples);
#endif

static void btpcm_ibiza_hrtimer_callback(void *p_opaque);

static int btpcm_ibiza_pcm_available(struct btpcm_ibiza_ccb *p_ccb);
static void btpcm_ibiza_pcm_read(struct btpcm_ibiza_ccb *p_ccb, void *p_dest_buffer, int nb_frames);

static int btpcm_ibiza_init(void);
static void btpcm_ibiza_exit(void);
static int btpcm_ibiza_open(int pcm_stream);
static int btpcm_ibiza_reopen(int pcm_stream);
static int btpcm_ibiza_close(int pcm_stream);
static int btpcm_ibiza_config(int pcm_stream, void *p_opaque, int frequency, int nb_channel,
        int bits_per_sample, void (*pcm_callback) (int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames));
static int btpcm_ibiza_start(int pcm_stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization);
static int btpcm_ibiza_stop(int pcm_stream);
static void btpcm_ibiza_synchronization(int pcm_stream);

static int btpcm_ibiza_get_left_write_offset(struct btpcm_ibiza_ccb *p_ccb);

static int btpcm_ibiza_set_shm_addr(unsigned int base_addr, unsigned int access_point_addr,
       unsigned int buff_size, unsigned int frame_size_addr, unsigned int enable_m_add );

/* Functions in charge of /proc */
static int btpcm_ibiza_file_open(struct inode *inode, struct file *file);
ssize_t btpcm_ibiza_file_write(struct file *file, const char *buf, size_t count,
        loff_t *pos);
static int btpcm_ibiza_file_show(struct seq_file *s, void *v);

/* Exported BTPCM operation. BTUSB calls these functions */
const struct btpcm_op btpcm_operation =
{
    btpcm_ibiza_init,
    btpcm_ibiza_exit,
    btpcm_ibiza_open,
    btpcm_ibiza_close,
    btpcm_ibiza_config,
    btpcm_ibiza_start,
    btpcm_ibiza_stop,
    btpcm_ibiza_set_shm_addr,
    btpcm_ibiza_synchronization,
};

/* This structure gather "functions" that manage the /proc file */
static struct file_operations btpcm_ibiza_file_ops =
{
    .owner   = THIS_MODULE,
    .open    = btpcm_ibiza_file_open,
    .read    = seq_read,
    .write   = btpcm_ibiza_file_write,
    .llseek  = seq_lseek,
    .release = single_release
};


/*******************************************************************************
 **
 ** Function        btpcm_ibiza_init
 **
 ** Description     This function is called when the /proc file is open.
 **
 ** Returns         Status
 **
 *******************************************************************************/
static int btpcm_ibiza_file_open(struct inode *inode, struct file *file)
{
    return single_open(file, btpcm_ibiza_file_show, PDE_DATA(inode));
};

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_file_write
 **
 ** Description     This function is called when User Space writes in /proc file.
 **
 ** Returns         Status
 **
 *******************************************************************************/
ssize_t btpcm_ibiza_file_write(struct file *file, const char *buf,
        size_t count, loff_t *pos)
{
    unsigned char cmd;
    int status;

    /* copy the first byte from the data written (the command) */
    if (copy_from_user(&cmd, buf, 1))
    {
        return -EFAULT;
    }

    /* Print the command */
    BTPCM_INFO("Command='%c'\n", cmd);

    switch (cmd)
    {
    case BTPCM_IBIZA_CMD_DBG:
#ifdef BTPCM_IBIZA_TONE
        BTPCM_INFO("btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
#else
        BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
        BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
        BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
        BTPCM_INFO("ibiza_enable_mark_addr=0x%x\n", (int)ibiza_enable_mark_addr);		
#endif
        break;

    case BTPCM_IBIZA_CMD_REOPEN:
        status = btpcm_ibiza_reopen(0);
        if (status < 0)
        {
            count = -1;
        }
        break;

    default:
        BTPCM_ERR("Unknown command=%d\n", cmd);
        break;
    }

    return count;
}

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_file_show
 **
 ** Description     This function is called for each "step" of a sequence.
 **
 ** Returns         None
 **
 *******************************************************************************/
static int btpcm_ibiza_file_show(struct seq_file *s, void *v)
{
    struct btpcm_ibiza_ccb *p_ccb;

    /* Get Reference to the first PCM CCB */
    p_ccb = &btpcm_ibiza_cb.ccb[0];

    if (p_ccb->opened)
    {
        seq_printf(s, "Stream=0 Opened\n");
    }
    else
    {
        seq_printf(s, "Stream=0 Closed\n");
    }

    if (atomic_read(&p_ccb->started))
    {
        seq_printf(s, "Stream=0 Started\n");
    }
    else
    {
        seq_printf(s, "Stream=0 Stopped\n");
    }

#ifdef BTPCM_IBIZA_TONE
    seq_printf(s, "btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
#else
    if (p_ccb->opened)
    {
        seq_printf(s, "BTPCM Ibiza is currently opened with:\n");
        seq_printf(s, "Current ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
        seq_printf(s, "Current ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
        seq_printf(s, "Current ibiza_pcm_left_write_phy_addr=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
        seq_printf(s, "Current ibiza_enable_mark_addr=0x%x\n", (int)ibiza_enable_mark_addr);
    }
    seq_printf(s, "BTPCM Ibiza is will be reopened with:\n");
    seq_printf(s, "ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
    seq_printf(s, "ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    seq_printf(s, "ibiza_pcm_left_write_phy_addr=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
    seq_printf(s, "ibiza_enable_mark_addr=0x%x\n", (int)ibiza_enable_mark_addr);
#endif

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_init
 **
 ** Description      BTPCM Tone Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_init(void)
{
    int err = 0;
    struct proc_dir_entry *entry = NULL;

    BTPCM_INFO("The Linux time reference (HZ) is %d\n", HZ);

    memset(&btpcm_ibiza_cb, 0, sizeof(btpcm_ibiza_cb));

    /* Initialize High Resolution Timer */
    err = btpcm_hrtimer_init();
    if (err < 0)
    {
        return -EINVAL;
    }

    /* Create /proc/btpcm entry */
    entry = proc_create_data(BTPCM_IBIZA_PROC_NAME, S_IRUGO | S_IWUGO, NULL,
            &btpcm_ibiza_file_ops, NULL);

    if (entry)
    {
        BTPCM_INFO("/proc/%s entry created\n", BTPCM_IBIZA_PROC_NAME);
    }
    else
    {
        BTPCM_ERR("Failed to create /proc/%s entry\n", BTPCM_IBIZA_PROC_NAME);
        return -EINVAL;
    }

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=%u\n", btpcm_ibiza_param_test);
    btpcm_ibiza_tone_init();
#else
    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr(Default)=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size(Default)=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr(Default)=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr(Default)=0x%x\n", (int)ibiza_enable_mark_addr);
#endif


    return err;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_exit
 **
 ** Description      BTPCM ibiza Exit function.
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_exit(void)
{
    int stream;
    struct btpcm_ibiza_ccb *p_ccb = &btpcm_ibiza_cb.ccb[0];

    BTPCM_INFO("Enter\n");

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=%ul\n", btpcm_ibiza_param_test);
#endif

    /* Stop every started ibiza */
    for (stream = 0 ; stream < BTPCM_NB_STREAM_MAX ; stream++, p_ccb++)
    {
        if (atomic_read(&p_ccb->started))
        {
            btpcm_ibiza_stop(stream);
        }
    }

    btpcm_hrtimer_exit();                   /* Stop High Resolution Timer */

    remove_proc_entry(BTPCM_IBIZA_PROC_NAME, NULL);
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_open
 **
 ** Description      BTPCM Tone Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_open(int pcm_stream)
{
    struct btpcm_ibiza_ccb *p_ccb;
/*#ifndef BTPCM_IBIZA_TONE
    struct resource *p_resource;
#endif*/

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    /* Get Reference to this PCM CCB */
    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];

    if (p_ccb->opened)
    {
        BTPCM_ERR("Stream=%d already opened\n", pcm_stream);
        return -EBUSY;
    }

#ifdef BTPCM_IBIZA_TONE
    p_ccb->p_left_io_pcm  = &btpcm_ibiza_tone_buf[0];
#else /* BTPCM_IBIZA_TONE */
#if 0
    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr=0x%x\n", (int)ibiza_enable_mark_addr);

    /* Save the Physical addresses which will be used */
    p_ccb->ibiza_pcm_buf_left_phy_addr = ibiza_pcm_buf_left_phy_addr;
    p_ccb->ibiza_pcm_buf_size = ibiza_pcm_buf_size;
    p_ccb->ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr;
    p_ccb->ibiza_enable_mark_addr = ibiza_enable_mark_addr;

    /* Reserve the Memory Region (both Left and Right PCM Buffers) */
    p_resource = request_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2, "BTPCM Buffer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (PCM buffer) is unavailable for Stream=%d\n", pcm_stream);
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_pcm = ioremap(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2);
    if (!p_ccb->p_left_io_pcm)
    {
        BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        return -ENOMEM;
    }

    /* Reserve the Memory Region (Left Write Pointer) */
    p_resource = request_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr,
            sizeof(void *), "BTPCM LeftWritePointer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (LeftWritePtr) is unavailable for Stream=%d\n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);        
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
	 //release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_ptr = ioremap(p_ccb->ibiza_pcm_left_write_phy_addr,
            sizeof(void *));
    if (!p_ccb->p_left_io_ptr)
    {
        BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -ENOMEM;
    }

     /* Reserve the Memory Region (Enable Mark Address) */
    p_resource = request_mem_region(p_ccb->ibiza_enable_mark_addr,
            sizeof(void *), "BTPCM EnableMardPointer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (EnableMark) is unavailable for Stream=%d\n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
	 iounmap(p_ccb->p_left_io_ptr);
	 release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
	 release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_enablemark = ioremap(p_ccb->ibiza_enable_mark_addr,
            sizeof(void *));
    if (!p_ccb->p_left_io_enablemark)
    {
        BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
        iounmap(p_ccb->p_left_io_ptr);		
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));
        return -ENOMEM;
    }
#endif

#endif /* !BTPCM_IBIZA_TONE */

    /* Mark this Channel as opened */
    p_ccb->opened = 1;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_open
 **
 ** Description      BTPCM Tone Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_reopen(int pcm_stream)
{
    struct btpcm_ibiza_ccb *p_ccb;
/*#ifndef BTPCM_IBIZA_TONE //removed by Moon
    struct resource *p_resource;
#endif*/

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    /* Get Reference to this PCM CCB */
    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];

    if (p_ccb->opened == 0)
    {
        BTPCM_ERR("Stream=%d Not Opened\n", pcm_stream);
        return -EBUSY;
    }

    if (atomic_read(&p_ccb->started))
    {
        BTPCM_ERR("Stream=%d Not Stopped\n", pcm_stream);
        return -EBUSY;
    }

    BTPCM_INFO("(Re)Open, internally, the BTPCM Ibiza port\n");

#ifdef BTPCM_IBIZA_TONE
    BTPCM_INFO("btpcm_ibiza_param_test=0x%x(%u)\n", btpcm_ibiza_param_test, btpcm_ibiza_param_test);
    BTPCM_INFO("Nothing to do for Ibiza Tone Simulation\n");
#else /* BTPCM_IBIZA_TONE */
    /*
     * Firstly, unmap/release memory regions
     */
#if 0 //removed by Moon
    /* Unmap the IO Memory (Buffer) */
    iounmap(p_ccb->p_left_io_pcm);

    /* Dereserve the Memory Region (both Left and Right Buffers) */
    release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2);

    /* Unmap the IO Memory (Left Write Pointer) */
    iounmap(p_ccb->p_left_io_ptr);

    /* Dereserve the Memory Region*/
    release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));

    /* Unmap the IO Memory (Enable Mark) */
    iounmap(p_ccb->p_left_io_enablemark);

    /* Dereserve the Memory Region*/
    release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));
#endif
    /*
     * Secondly, reserve/remap memory regions
     */
 #if 0 //removed by Moon
    BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
    BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
    BTPCM_INFO("ibiza_pcm_left_write_phy_addr=0x%x\n", (int)ibiza_pcm_left_write_phy_addr);
    BTPCM_INFO("ibiza_enable_mark_addr=0x%x\n", (int)ibiza_enable_mark_addr);

    /* Save the Physical addresses which will be used */
    p_ccb->ibiza_pcm_buf_left_phy_addr = ibiza_pcm_buf_left_phy_addr;
    p_ccb->ibiza_pcm_buf_size = ibiza_pcm_buf_size;
    p_ccb->ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr;
    p_ccb->ibiza_enable_mark_addr = ibiza_enable_mark_addr;

    /* Reserve the Memory Region (both Left and Right PCM Buffers) */
    p_resource = request_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2, "BTPCM Buffer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (PCM buffer) is unavailable for Stream=%d\n", pcm_stream);
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_pcm = ioremap(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2);
    if (!p_ccb->p_left_io_pcm)
    {
        BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr, p_ccb->ibiza_pcm_buf_size * 2);
        return -ENOMEM;
    }

    /* Reserve the Memory Region (Left Write Pointer) */
    p_resource = request_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr,
            sizeof(void *), "BTPCM LeftWritePointer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (LeftWritePtr) is unavailable for Stream=%d\n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        //release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_ptr = ioremap(p_ccb->ibiza_pcm_left_write_phy_addr,
            sizeof(void *));
    if (!p_ccb->p_left_io_ptr)
    {
        BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -ENOMEM;
    }

    /* Reserve the Memory Region (Enable Mark Address) */
    p_resource = request_mem_region(p_ccb->ibiza_enable_mark_addr,
            sizeof(void *), "BTPCM EnableMardPointer Ibiza");
    if (!p_resource)
    {
        BTPCM_ERR("Resources (EnableMark) is unavailable for Stream=%d\n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
	 iounmap(p_ccb->p_left_io_ptr);
	 release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
	 release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        return -EBUSY;
    }

    /* Remap the IO Memory */
    p_ccb->p_left_io_enablemark = ioremap(p_ccb->ibiza_enable_mark_addr,
            sizeof(void *));
    if (!p_ccb->p_left_io_enablemark)
    {
        BTPCM_ERR("(p_left_io_enablemark) ioremap failed for Stream=%d \n", pcm_stream);
        iounmap(p_ccb->p_left_io_pcm);
        iounmap(p_ccb->p_left_io_ptr);		
        release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
                p_ccb->ibiza_pcm_buf_size * 2);
        release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
        release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));
        return -ENOMEM;
    }
#endif
	
#endif /* !BTPCM_IBIZA_TONE */

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_close
 **
 ** Description      BTPCM Tone Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_close(int pcm_stream)
{
    struct btpcm_ibiza_ccb *p_ccb;

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    /* Get Reference to this PCM CCB */
    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];

    if (!p_ccb->opened)
    {
        BTPCM_ERR("Stream=%d was not opened\n", pcm_stream);
        return -EBUSY;
    }

#if 0
#ifndef BTPCM_IBIZA_TONE
    /* Unmap the IO Memory (Buffer) */
    iounmap(p_ccb->p_left_io_pcm);

    /* Dereserve the Memory Region (both Left and Right Buffers) */
    release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
            p_ccb->ibiza_pcm_buf_size * 2);

    /* Unmap the IO Memory (Left Write Pointer) */
    iounmap(p_ccb->p_left_io_ptr);

    /* Dereserve the Memory Region*/
    release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));

     /* Unmap the IO Memory (Enable Mark Pointer) */
    iounmap(p_ccb->p_left_io_enablemark);

    /* Dereserve the Memory Region*/
    release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));
	
#endif
#endif
    /* Mark this Channel as closed */
    p_ccb->opened = 0;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_config
 **
 ** Description      BTPCM Tone Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_config(int pcm_stream, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
        void (*callback) (int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames))
{
    struct btpcm_ibiza_ccb *p_ccb;

    BTPCM_DBG("stream=%d freq=%d nb_channel=%d bps=%d cback=%p\n",
            pcm_stream, frequency, nb_channel, bits_per_sample, callback);

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    if (callback == NULL)
    {
        BTPCM_ERR("Null Callback\n");
        return -EINVAL;
    }

    if (p_opaque == NULL)
    {
        BTPCM_ERR("Null p_opaque\n");
        return -EINVAL;
    }

    if (nb_channel != 2)
    {
        BTPCM_ERR("nb_channel=%d unsupported\n", nb_channel);
        return -EINVAL;
    }

    if (bits_per_sample != 16)
    {
        BTPCM_ERR("bits_per_sample=%d unsupported\n", bits_per_sample);
        return -EINVAL;
    }

    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];
    if (atomic_read(&p_ccb->started))
    {
        BTPCM_ERR("Tone stream=%d already started\n", pcm_stream);
        return -EINVAL;
    }

    /* Save the configuration */
    p_ccb->callback = callback;
    p_ccb->p_opaque = p_opaque;
    p_ccb->frequency = frequency;
    p_ccb->bits_per_sample = bits_per_sample;
    p_ccb->nb_channel = nb_channel;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_set_shm_addr
 **
 ** Description      BTPCM Tone function to receive PCM buffer addresses from
 **                  ALSA Lib.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_set_shm_addr(unsigned int base_addr, unsigned int access_point_addr, unsigned int buff_size, unsigned int frame_size_addr, unsigned int enable_m_addr )
{
#if 0
    ibiza_pcm_buf_left_phy_addr = base_addr;

    ibiza_pcm_access_point_address = access_point_addr;

    ibiza_pcm_buf_size = buff_size;

    ibiza_pcm_m_address = enable_m_addr;

    BTPCM_INFO("Base Addr[0x%x]\n"
                "Access Point Addr[0x%x]\n"
                "PCM Buffer Size[0x%x]\n"
                "Enable Mark Address[0x%x] \n",
                (unsigned int)ibiza_pcm_buf_left_phy_addr,
                (unsigned int)ibiza_pcm_access_point_address,
                (unsigned int)ibiza_pcm_buf_size,
                (unsigned int)ibiza_pcm_m_address);
#endif
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_start
 **
 ** Description      BTPCM Tone Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_start(int pcm_stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    struct btpcm_ibiza_ccb *p_ccb;
    int err;
    uint64_t period_ns;
#ifdef BTPCM_IBIZA_DEBUG
    uint64_t temp_period_ns;
#endif
#ifndef BTPCM_IBIZA_TONE
    struct resource *p_resource; //added by Moon
#endif

    uint64_t remapSize = sizeof(void *);

    BTPCM_INFO("pcm_stream=%d nb_pcm_frames=%d nb_pcm_packets=%d sync=%d\n",
            pcm_stream, nb_pcm_frames, nb_pcm_packets, synchronization);

    /* BAV (synchronization) not yet supported for Ibiza */
    if (synchronization)
    {
        BTPCM_ERR("BAV (synchronization) not yet supported for Ibiza\n");
        return -EINVAL;
    }

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    if ((nb_pcm_frames == 0) || (nb_pcm_packets == 0))
    {
        BTPCM_ERR("Bad nb_pcm_frames=%d nb_pcm_packets=%d\n", nb_pcm_frames, nb_pcm_packets);
        return -EINVAL;
    }

    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];

//added by Moon
	BTPCM_INFO("ibiza_pcm_buf_left_phy_addr=0x%x\n", (int)ibiza_pcm_buf_left_phy_addr);
	BTPCM_INFO("ibiza_pcm_buf_size=%d\n", (int)ibiza_pcm_buf_size);
/*
	BTPCM_INFO("ibiza_pcm_access_point_address=0x%x\n",
					(int)ibiza_pcm_access_point_address);
	BTPCM_INFO("ibiza_pcm_m_address=0x%x\n", (int)ibiza_pcm_m_address);
*/
	/* Save the Physical addresses which will be used */
	p_ccb->ibiza_pcm_buf_left_phy_addr = ibiza_pcm_buf_left_phy_addr;
	p_ccb->ibiza_pcm_buf_size = ibiza_pcm_buf_size;
	p_ccb->ibiza_pcm_left_write_phy_addr = ibiza_pcm_left_write_phy_addr;
	p_ccb->ibiza_enable_mark_addr = ibiza_enable_mark_addr;


/////Buffer Start Address/////

	/* Reserve the Memory Region (both Left and Right PCM Buffers) */
	p_resource = request_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr, p_ccb->ibiza_pcm_buf_size * 2, "BTPCM Buffer Ibiza");
	if (!p_resource)
	{
		BTPCM_ERR("Resources (PCM buffer) is unavailable for Stream=%d\n", pcm_stream);
		return -EBUSY;
	}

	/* Remap the IO Memory */
	p_ccb->p_left_io_pcm = ioremap(p_ccb->ibiza_pcm_buf_left_phy_addr,
	p_ccb->ibiza_pcm_buf_size * 2);
	if (!p_ccb->p_left_io_pcm)
	{
		BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
		release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
		p_ccb->ibiza_pcm_buf_size * 2);
		return -ENOMEM;
	}

/////Write Pointer/////

#if 1
	/* Store Left Write Pointer */
	p_ccb->p_left_io_ptr = (unsigned int*) p_ccb->ibiza_pcm_left_write_phy_addr;
	/* Store Enable Mark Address */
	p_ccb->p_left_io_enablemark = (unsigned int*) p_ccb->ibiza_enable_mark_addr;
#endif
#if 1
	/* Reserve the Memory Region (Left Write Pointer) */
	p_resource = request_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *), "BTPCM LeftWritePointer Ibiza");
	if (!p_resource)
	{
		BTPCM_ERR("Resources (LeftWritePtr) is unavailable for Stream=%d\n", pcm_stream);
		iounmap(p_ccb->p_left_io_pcm);
		release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		return -EBUSY;
	}

	/* Remap the IO Memory */
	p_ccb->p_left_io_ptr = ioremap(p_ccb->ibiza_pcm_left_write_phy_addr, remapSize);
	if (!p_ccb->p_left_io_ptr)
	{
		BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
		iounmap(p_ccb->p_left_io_pcm);
		release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
		p_ccb->ibiza_pcm_buf_size * 2);
		release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		return -ENOMEM;
	}

/////nable Mark Address/////
	/* Reserve the Memory Region (Enable Mark Address) */
	p_resource = request_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *), "BTPCM EnableMardPointer Ibiza");
	if (!p_resource)
	{
		BTPCM_ERR("Resources (EnableMark) is unavailable for Stream=%d\n", pcm_stream);
		iounmap(p_ccb->p_left_io_pcm);
		iounmap(p_ccb->p_left_io_ptr);
		release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
		p_ccb->ibiza_pcm_buf_size * 2);
		release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		return -EBUSY;
	}

	/* Remap the IO Memory */
	p_ccb->p_left_io_enablemark = ioremap(p_ccb->ibiza_enable_mark_addr, remapSize);
	if (!p_ccb->p_left_io_enablemark)
	{
		BTPCM_ERR("ioremap failed for Stream=%d \n", pcm_stream);
		iounmap(p_ccb->p_left_io_pcm);
		iounmap(p_ccb->p_left_io_ptr);		
		release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr,
		p_ccb->ibiza_pcm_buf_size * 2);
		release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
		release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));
		return -ENOMEM;
	}
#endif
//
    if (atomic_read(&p_ccb->started))
    {
        BTPCM_WRN("Tone stream=%d already started\n", pcm_stream);
        return -EINVAL;
    }

    /* Allocate a PCM buffer able to contain nb_frames samples (stereo,16 bits) */
    /* Add one more frame to be able to absorb overrun (HW goes a bit too fast) */
    p_ccb->p_buf = kmalloc(nb_pcm_frames * (nb_pcm_packets + 1) * BTPCM_FRAME_SIZE, GFP_KERNEL);
    if (p_ccb->p_buf == NULL)
    {
        BTPCM_ERR("Unable to allocate buffer (size=%d)\n",
                (int)(nb_pcm_frames * (nb_pcm_packets + 1) * BTPCM_FRAME_SIZE));
        return -ENOMEM;
    }

    /* If synchronization, we don't need timer */
    if (synchronization == 0)
    {
        if (p_ccb->hr_timer == NULL)
        {
            p_ccb->hr_timer =  btpcm_hrtimer_alloc(btpcm_ibiza_hrtimer_callback,
                    (void *)(uintptr_t)pcm_stream);
            if (p_ccb->hr_timer == NULL)
            {
                BTPCM_ERR("No more timer\n");
                return -EINVAL;
            }
        }
    }

    /* High Resolution Timer */
    period_ns = (uint64_t)nb_pcm_frames * (uint64_t)nb_pcm_packets;
    period_ns *= 1000;  /* usec */
    period_ns *= 1000;  /* msec */
    period_ns *= 1000;  /* sec */
    do_div(period_ns, p_ccb->frequency);

#ifdef BTPCM_IBIZA_DEBUG
    temp_period_ns = period_ns;
    //temp_period_ns = period_ns/2;
    do_div(temp_period_ns, 1000000);/* convert to msec for debug */
    p_ccb->timer_duration = (int)temp_period_ns;
    BTPCM_INFO("temp_period_ns=%d(ms)\n", p_ccb->timer_duration);
    p_ccb->jiffies = jiffies;  /* Save the current timestamp (for debug) */
#endif

    p_ccb->nb_frames = nb_pcm_frames;
    p_ccb->nb_packets = nb_pcm_packets;
    BTPCM_INFO("HR Timer_duration=%llu(ns) nb_frames=%d\n", period_ns, p_ccb->nb_frames);
    //BTPCM_INFO("HR Timer_duration=%llu(ns) nb_frames=%d\n", period_ns/2, p_ccb->nb_frames);
    
    /* Mark the stream as started */
    atomic_set(&p_ccb->started, 1);

    /* Read (and save) the Write Offset */
    /* Read and Write Offsets are at the same location (empty buffer) */
    p_ccb->left_read_offset = btpcm_ibiza_get_left_write_offset(p_ccb);
	
    /* If no synchronization, we need to start a periodic timer */
    if (synchronization == 0)
    {
        /* Start the HR timer */
	 err = btpcm_hrtimer_start(p_ccb->hr_timer, period_ns);
	 //err = btpcm_hrtimer_start(p_ccb->hr_timer, period_ns/2);
        if (err < 0)
        {
            BTPCM_ERR("Unable to start timer\n");
            kfree(p_ccb->p_buf);
            p_ccb->p_buf = NULL;
            atomic_set(&p_ccb->started, 0);
            return -EINVAL;
        }
    }
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_stop
 **
 ** Description      BTPCM Tone Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_ibiza_stop(int pcm_stream)
{
    struct btpcm_ibiza_ccb *p_ccb;
    int err;

    BTPCM_INFO("stream=%d\n", pcm_stream);

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];
    if (!atomic_read(&p_ccb->started))
    {
        BTPCM_ERR("Tone stream=%d not started\n", pcm_stream);
        return -EINVAL;
    }

    /* Mark the ibiza as stopped */
    atomic_set(&p_ccb->started, 0);

    if (p_ccb->hr_timer)
    {
        err = btpcm_hrtimer_stop(p_ccb->hr_timer);
        if (err < 0)
        {
            BTPCM_ERR("Unable to stop timer\n");
            return -EINVAL;
        }
        btpcm_hrtimer_free(p_ccb->hr_timer);
        p_ccb->hr_timer = NULL;
    }

    kfree(p_ccb->p_buf);
    p_ccb->p_buf = NULL;

//added by Moon
#ifndef BTPCM_IBIZA_TONE
	/* Unmap the IO Memory (Buffer) */
	iounmap(p_ccb->p_left_io_pcm);
	/* Dereserve the Memory Region (both Left and Right Buffers) */
	release_mem_region(p_ccb->ibiza_pcm_buf_left_phy_addr, p_ccb->ibiza_pcm_buf_size * 2);

	iounmap(p_ccb->p_left_io_ptr);
	release_mem_region(p_ccb->ibiza_pcm_left_write_phy_addr, sizeof(void *));
	iounmap(p_ccb->p_left_io_enablemark);
	release_mem_region(p_ccb->ibiza_enable_mark_addr, sizeof(void *));

#endif
//
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_synchronization
 **
 ** Description      BTPCM Tone Synchronization function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_ibiza_synchronization(int pcm_stream)
{
    BTPCM_ERR("Not Yet Implemented\n");

#if 0
    struct btpcm_ibiza_ccb *p_ccb;
    int delta_error;

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return;
    }

    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];
    if (!atomic_read(&p_ccb->started))
    {
        BTPCM_DBG("stream=%d stopped.\n", pcm_stream);
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

    if (p_ccb->p_buf == NULL)
    {
        BTPCM_ERR("p_buff is NULL stream=%d\n", pcm_stream);
        atomic_set(&p_ccb->started, 0);
        return;
    }

    if (p_ccb->callback == NULL)
    {
        BTPCM_ERR("callback is NULL stream=%d\n", pcm_stream);
        atomic_set(&p_ccb->started, 0);
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_ccb->jiffies);
    delta_error -= p_ccb->timer_duration;
    p_ccb->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 10)
    {
        BTPCM_ERR("delta_error=%d\n", delta_error);
    }

    /* TODO */
    /* Fill the buffer with the requested number of frames */
    btpcm_ibiza_read(pcm_stream,
            (short *)p_ccb->p_buf,
            p_ccb->nb_frames * BTPCM_FRAME_SIZE,
            &p_ccb->sinus_index);

    p_ccb->callback(pcm_stream, p_ccb->p_opaque, p_ccb->p_buf, p_ccb->nb_frames);
#endif
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_hrtimer_callback
 **
 ** Description      BTPCM Tone High Resolution Timer callback.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_ibiza_hrtimer_callback(void *p_opaque)
{
    int pcm_stream = (int)(uintptr_t)p_opaque;
    struct btpcm_ibiza_ccb *p_ccb;
    int nb_pcm_frames;
    int nb_pcm_packets;
#ifdef BTPCM_IBIZA_DEBUG
    int delta_error;
#endif

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return;
    }

    p_ccb = &btpcm_ibiza_cb.ccb[pcm_stream];
    if (!atomic_read(&p_ccb->started))
    {
        BTPCM_DBG("stream=%d stopped.\n", pcm_stream);
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

    if (p_ccb->p_buf == NULL)
    {
        BTPCM_ERR("p_buff is NULL stream=%d\n", pcm_stream);
        atomic_set(&p_ccb->started, 0);
        return;
    }

    if (p_ccb->callback == NULL)
    {
        BTPCM_ERR("callback is NULL stream=%d\n", pcm_stream);
        atomic_set(&p_ccb->started, 0);
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

#ifdef BTPCM_IBIZA_DEBUG
    /* Calculate the timer error (for debug) */
    delta_error = (int)jiffies_to_msecs(jiffies - p_ccb->jiffies);
    delta_error -= p_ccb->timer_duration;
    p_ccb->jiffies = jiffies;

    /* If the timer elapsed too late */
    if (delta_error > 16)
    {
        BTPCM_ERR("timer expired with delta_error=%d\n", delta_error);
    }
#endif

    nb_pcm_frames = 0;
    nb_pcm_packets = 0;

#ifdef BTPCM_IBIZA_TONE
    btpcm_ibiza_tone_simulate_write(p_ccb->nb_frames * p_ccb->nb_packets);
#endif

    /* While enough PCM samples available */
    while((btpcm_ibiza_pcm_available(p_ccb) >= p_ccb->nb_frames) &&
          (nb_pcm_packets < (p_ccb->nb_packets + 1)))
    {
        /* Read (and convert) the HW PCM buffer */
        btpcm_ibiza_pcm_read(p_ccb, p_ccb->p_buf + (nb_pcm_packets * p_ccb->nb_frames * BTPCM_FRAME_SIZE), p_ccb->nb_frames);
        nb_pcm_frames += p_ccb->nb_frames;
        nb_pcm_packets++;
    }

    p_ccb->callback(pcm_stream, p_ccb->p_opaque, p_ccb->p_buf, nb_pcm_frames);
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_pcm_available
 **
 ** Description      Get number of PCM Frames available in the HW buffer
 **
 ** Returns          void
 **
 *******************************************************************************/
static int btpcm_ibiza_pcm_available(struct btpcm_ibiza_ccb *p_ccb)
{
    int left_write_offset;
    int pcm_buf_size;
    static int cnt = 0;

    left_write_offset = btpcm_ibiza_get_left_write_offset(p_ccb);
    if (left_write_offset < 0)
    {
        return 0;   /* In case of error, indicate that no PCM available */
    }

    /* If Read and Write offset at same position => empty buffer */
    if (left_write_offset == p_ccb->left_read_offset)
    {
        BTPCM_DBG("Empty HW PCM buffer\n");
	 cnt++;
	 if(cnt > 200)
	 {
	 	BTPCM_INFO("Empty HW PCM buffer\n");
	       cnt = 0;
	 }	
        return 0;
    }
    cnt = 0;

    if (left_write_offset > p_ccb->left_read_offset)
    {
        BTPCM_DBG("1 returns=%ld\n", (left_write_offset - p_ccb->left_read_offset) / BTPCM_HW_PCM_SAMPLE_SIZE);
        return ((left_write_offset - p_ccb->left_read_offset) / BTPCM_HW_PCM_SAMPLE_SIZE);
    }
    else
    {
#ifdef BTPCM_IBIZA_TONE
        pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
#else
        pcm_buf_size = p_ccb->ibiza_pcm_buf_size;
#endif
        BTPCM_DBG("2 returns=%ld\n", (left_write_offset - p_ccb->left_read_offset + pcm_buf_size) / BTPCM_HW_PCM_SAMPLE_SIZE);
        return ((left_write_offset - p_ccb->left_read_offset + pcm_buf_size) / BTPCM_HW_PCM_SAMPLE_SIZE);
    }
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_pcm_read
 **
 ** Description      Fill up a PCM buffer with a HW PCM data
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_pcm_read(struct btpcm_ibiza_ccb *p_ccb, void *p_dest_buffer, int nb_frames)
{
    uint32_t *p_dest = (uint32_t *)p_dest_buffer;
    uint8_t *p_left_src = (uint8_t *)(p_ccb->p_left_io_pcm + p_ccb->left_read_offset);
    int pcm_buf_size;
    uint32_t temp32;

#ifdef BTPCM_IBIZA_TONE
    pcm_buf_size = BTPCM_IBIZA_PCM_BUF_SIZE;
#else
    pcm_buf_size = p_ccb->ibiza_pcm_buf_size;
#endif

    //p_right_src = p_left_src + pcm_buf_size;

    /* Sanity */
    if ((p_ccb->left_read_offset < 0) ||
        (p_ccb->left_read_offset >= pcm_buf_size))
    {
        //BTPCM_ERR("Bad Left Read Offset=%d\n", p_ccb->left_read_offset);
        return;
    }

    /* Update Left Read Offset */
    p_ccb->left_read_offset += nb_frames * BTPCM_HW_PCM_SAMPLE_SIZE;
    if (p_ccb->left_read_offset >= pcm_buf_size)
    {
        p_ccb->left_read_offset -= pcm_buf_size;
    }

    while(nb_frames--)
    {
        temp32 = (uint32_t)(IOREAD32(p_left_src));
        p_left_src += BTPCM_HW_PCM_SAMPLE_SIZE;
        *p_dest++ = (uint32_t)temp32;

        if (p_left_src >= (uint8_t *)(p_ccb->p_left_io_pcm + pcm_buf_size))
        {
            /* Set Left Read pointer to the beginning of the Left Buffer */
            p_left_src = (uint8_t *)p_ccb->p_left_io_pcm;
        }
    }
}

/*******************************************************************************
 **
 ** Function        btpcm_ibiza_get_left_write_offset
 **
 ** Description     Read the LeftWritePointer and convert it to an offset (from the
 **                 Beginning of the Left PCM buffer)
 **
 ** Returns         offset (-1 if error)
 **
 *******************************************************************************/
static int btpcm_ibiza_get_left_write_offset(struct btpcm_ibiza_ccb *p_ccb)
{
    static int cnt = 0;
    //static int cnt2 = 0;
    int left_write_offset=0;
#ifndef BTPCM_IBIZA_TONE
    void *p_left_write_pointer;
    void *p_enable_mark_pointer;

#endif

    /* Sanity */
    if (!p_ccb)
    {
        BTPCM_ERR("p_ccb is NULL\n");
        return -1;
    }

#ifdef BTPCM_IBIZA_TONE
    left_write_offset = btpcm_ibiza_tone_write_offset;

    /* Sanity */
    if ((left_write_offset < 0) ||
        (left_write_offset >= BTPCM_IBIZA_PCM_BUF_SIZE))
    {
        BTPCM_ERR("Wrong pp_left_write_pointer=%d\n", left_write_offset);
        return -1;
    }
#else
    /* Read 32 bits of Left Write Pointer */

    p_left_write_pointer = (void *)(uintptr_t)IOREAD32(p_ccb->p_left_io_ptr);
	
    p_enable_mark_pointer =  (void *)(uintptr_t)IOREAD16(p_ccb->p_left_io_enablemark);

    /*cnt++;
    if(cnt > 100)
    {
        BTPCM_INFO("delay(%p)\n", p_enable_mark_pointer);	
        cnt = 0;
    }*/


    /* Sanity */
    if ((p_left_write_pointer < (void *)(uintptr_t)p_ccb->ibiza_pcm_buf_left_phy_addr) ||
        (p_left_write_pointer >= (void *)(uintptr_t)(p_ccb->ibiza_pcm_buf_left_phy_addr + p_ccb->ibiza_pcm_buf_size)))
    {
	cnt++;
	if(cnt > 300)
	{
		BTPCM_ERR("Wrong p_left_write_pointer:%p\n", p_left_write_pointer);
		cnt = 0;
	}
        return -1;
    }

    /* Calculate the offset from the beginning of the Left PCM Buffer */
    /* Note that this calculation is done using Physical Addresses */
    left_write_offset = p_left_write_pointer - (void *)(uintptr_t)p_ccb->ibiza_pcm_buf_left_phy_addr;

    /* Sanity */
    if ((left_write_offset < 0) ||
        (left_write_offset >= p_ccb->ibiza_pcm_buf_size))
    {
        BTPCM_ERR("Wrong pp_left_write_pointer=%d\n", left_write_offset);
        return -1;
    }

    if(p_ccb->p_enable_mark_pointer_before != p_enable_mark_pointer)
    {
        BTPCM_INFO("(%p)->(%p)_EmptyBuffer!", p_ccb->p_enable_mark_pointer_before, p_enable_mark_pointer);	
    	 p_ccb->left_read_offset = left_write_offset;
        //cnt2 = 8;
    }
    /*else
    {
	if(cnt2 >= 0)
	{
		//printk("drop buffer!!(%d)\n", cnt2);
	       p_ccb->left_read_offset = left_write_offset;
		cnt2--;
	}
    }*/

    p_ccb->p_enable_mark_pointer_before = p_enable_mark_pointer;

#endif

    return left_write_offset;
}

#ifdef BTPCM_IBIZA_TONE
/*******************************************************************************
 **
 ** Function         btpcm_ibiza_tone_init
 **
 ** Description      Init Tone
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_tone_init(void)
{
    int index;
    int sin_index;


    /* Generate a standard PCM stereo interlaced sinewave */
    for (index = 0, sin_index = 0; index < sizeof(btpcm_ibiza_tone_buf) ;
            index += BTPCM_HW_PCM_SAMPLE_SIZE, sin_index++)
    {
        if (index < sizeof(btpcm_ibiza_tone_buf) / 2)
        {
            /* Left Channel use a tone */
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 4)
            btpcm_ibiza_tone_buf[index] = 0xAA; /* LSB Unused */
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)btpcm_ibiza_sinwaves[0][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 2] = (uint8_t)(btpcm_ibiza_sinwaves[0][sin_index % 64] >> 8);
            btpcm_ibiza_tone_buf[index + 3] = 0xBB; /* MSB Unused (will be 0 in HW buffer) */
#endif
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 2)
            btpcm_ibiza_tone_buf[index + 0] = (uint8_t)btpcm_ibiza_sinwaves[0][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)(btpcm_ibiza_sinwaves[0][sin_index % 64] >> 8);
#endif
        }
        else
        {
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 4)
            /* Right Channel use another tone */
            btpcm_ibiza_tone_buf[index] = 0xCC; /* LSB Unused */
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)btpcm_ibiza_sinwaves[1][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 2] = (uint8_t)(btpcm_ibiza_sinwaves[1][sin_index % 64] >> 8);
            btpcm_ibiza_tone_buf[index + 3] = 0xDD; /* MSB Unused (will be 0 in HW buffer) */
#endif
#if (BTPCM_HW_PCM_SAMPLE_SIZE == 2)
            btpcm_ibiza_tone_buf[index + 0] = (uint8_t)btpcm_ibiza_sinwaves[1][sin_index % 64];
            btpcm_ibiza_tone_buf[index + 1] = (uint8_t)(btpcm_ibiza_sinwaves[1][sin_index % 64] >> 8);
#endif
        }
    }
    /* Set HW Write Pointer to the beginning of the buffer */
    btpcm_ibiza_tone_write_offset = 0;

    for (index = 0 ; index < BTPCM_NB_STREAM_MAX ; index++)
    {
        /* Set the Read Pointer to the beginning of the buffer */
        btpcm_ibiza_cb.ccb[index].left_read_offset = 0;
    }
}

/*******************************************************************************
 **
 ** Function         btpcm_ibiza_tone_simulate_write
 **
 ** Description      Simulate data in tone buffer (update write pointer)
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_ibiza_tone_simulate_write(int nb_pcm_samples)
{
    /* Sanity check (just to check) */
    if ((nb_pcm_samples * BTPCM_HW_PCM_SAMPLE_SIZE) >= BTPCM_IBIZA_PCM_BUF_SIZE)
    {
        BTPCM_ERR("Bad nb_pcm_samples=%d\n", nb_pcm_samples);
    }

    /* The buffer already contains tone. We just need to increment the Write Pointer */
    btpcm_ibiza_tone_write_offset += nb_pcm_samples * BTPCM_HW_PCM_SAMPLE_SIZE;

    /* Handle buffer wrap case */
    if (btpcm_ibiza_tone_write_offset >= BTPCM_IBIZA_PCM_BUF_SIZE)
    {
        btpcm_ibiza_tone_write_offset -= BTPCM_IBIZA_PCM_BUF_SIZE;
    }
}
#endif

