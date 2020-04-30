/*
 *  BTPCM ALSA soundcard
 *
 *  Copyright (c) 2013 by Patrick Coupe <pcoupe@broadcom.com>
 *
 *  Based on Dummy Sound driver from
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/initval.h>
#include "btpcm.h"
#include "btpcm_api.h"
#include "btpcm_hrtimer.h"

/* For 16bits per samples, Stereo, the PCM Frame size is 4 (2 * 2) */
#define BTPCM_FRAME_SIZE        (BTPCM_SAMPLE_16BIT_SIZE * BTPCM_SAMPLE_STEREO_SIZE)

#ifndef CONFIG_HIGH_RES_TIMERS
#error "High Resolution timer unsupported on this platform"
#endif

#define BTPCM_ALSA_CARD_DESC_MAX    80
#define BTPCM_ALSA_CARD_DESC    "Broadcom BTPCM ALSA Card %d"
#define BTPCM_ALSA_DRV_DESC     "Broadcom BTPCM ALSA Driver"
#define BTPCM_ALSA_PCM_PB_DESC  "Broadcom BTPCM ALSA PlayBack PCM"

/* Defaults values */
#ifndef BTPCM_ALSA_NB_STREAM_MAX
#define BTPCM_ALSA_NB_STREAM_MAX 1
#endif

#ifndef BTPCM_ALSA_BUFFER_SIZE
#define BTPCM_ALSA_BUFFER_SIZE  (20 * 1024) /* Around 400 ms of audio */
#endif
#ifndef BTPCM_ALSA_PERIOD_SIZE_MAX
#define BTPCM_ALSA_PERIOD_SIZE_MAX     (5 * 1024)   /* Around 100 ms of audio */
#endif
#ifndef BTPCM_ALSA_PERIOD_SIZE_MIN
#define BTPCM_ALSA_PERIOD_SIZE_MIN     64
#endif

/* ALSA Timer Operation */
struct btpcm_alsa_timer_ops {
    int (*create)(struct snd_pcm_substream *);
    void (*free)(struct snd_pcm_substream *);
    int (*prepare)(struct snd_pcm_substream *);
    int (*start)(struct snd_pcm_substream *);
    int (*stop)(struct snd_pcm_substream *);
    snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *);
};

/* ALSA Card */
struct btpcm_alsa {
    struct snd_card *card;
    struct snd_pcm *pcm;
    struct snd_pcm_hardware pcm_hw;
    const struct btpcm_alsa_timer_ops *timer_ops;
};

/* Ring Buffer definitions */
struct btpcm_alsa_rb
{
    spinlock_t lock;
    void *p_buf;
    volatile int in;
    volatile int out;
    int count;
};

/* Channel Control Block */
struct btpcm_alsa_ccb
{
    void (*callback) (int stream, void *p_opaque, void *p_buf, int nb_pcm_frames);
    void *p_opaque;
    int frequency;
    int nb_channel;
    int bits_per_sample;
    int nb_frames; /* Number of frames per packet */
    int nb_packets; /* Number of packets per timer period */
    int started;
    int opened;
    struct btpcm_alsa_rb rb;
    struct btpcm_hrtimer *hr_timer;
    void *p_buf;
};

/* Spin Lock/Unlock macros */
#define SPIN_LOCK(a, f)    spin_lock_irqsave(a, f)
#define SPIN_UNLOCK(a, f)  spin_unlock_irqrestore(a, f)

/* BGTPCM Control Block */
struct btpcm_alsa_cb_t
{
    struct btpcm_alsa_ccb ccb[BTPCM_ALSA_NB_STREAM_MAX];
};

/*
 * Global variables
 */
struct btpcm_alsa_cb_t btpcm_alsa_cb; /* BTPCM control block */

static int btpcm_alsa_card_table[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;  /* Index 0-MAX */

/* Definition of the HW parameter of the virtual ALSA Card */
static struct snd_pcm_hardware btpcm_alsa_hw = {
        .info = SNDRV_PCM_INFO_INTERLEAVED , /* Interleaved PCM */
        .formats = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits, Little Endian */
        .rates = SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,   /* 44.1KHz and 48KHz */
        .rate_min = 44100,
        .rate_max = 48000,
        .channels_min = 2,  /* Stereo only */
        .channels_max = 2,  /* Stereo only */
        .buffer_bytes_max = BTPCM_ALSA_BUFFER_SIZE,
        .period_bytes_min = BTPCM_ALSA_PERIOD_SIZE_MIN,
        .period_bytes_max = BTPCM_ALSA_PERIOD_SIZE_MAX,
        .periods_min = BTPCM_ALSA_PERIOD_SIZE_MIN,
        .periods_max = BTPCM_ALSA_PERIOD_SIZE_MAX,
        .fifo_size = 0, /* Unused by ALSA */
};


/* Local functions */
static int btpcm_alsa_card_create(int device);
static void btpcm_alsa_unregister_all(void);


static void btpcm_alsa_rb_write(int stream, void __user *p_data, int nb_byte);
static void btpcm_alsa_rb_read(int stream, void *p_data, int nb_byte);
static int btpcm_alsa_rb_get_count(int stream);

static void btpcm_alsa_pcm_hrtimer_callback(void *p_opaque);

#ifdef BTPCM_ALSA_TONE
static void btpcm_alsa_tone_read(short *p_buffer, int nb_bytes);
#endif

/*
 * hrtimer interface used by ALSA
 */
struct btpcm_alsa_hrtimer_pcm {
    ktime_t base_time;
    ktime_t period_time;
    atomic_t running;
    struct hrtimer timer;
    struct tasklet_struct tasklet;
    struct snd_pcm_substream *substream;
};

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_pcm_elapsed
 **
 ** Description      ALSA PCM HR Timer Tasklet.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_alsa_hrtimer_pcm_elapsed(unsigned long priv)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm = (struct btpcm_alsa_hrtimer_pcm *)priv;
    if (atomic_read(&dpcm->running))
        snd_pcm_period_elapsed(dpcm->substream);
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_callback
 **
 ** Description      ALSA PCM HR Timer Callback.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static enum hrtimer_restart btpcm_alsa_hrtimer_callback(struct hrtimer *timer)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm;

    dpcm = container_of(timer, struct btpcm_alsa_hrtimer_pcm, timer);
    if (!atomic_read(&dpcm->running))
        return HRTIMER_NORESTART;
    tasklet_schedule(&dpcm->tasklet);
    hrtimer_forward_now(timer, dpcm->period_time);
    return HRTIMER_RESTART;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_start
 **
 ** Description      ALSA PCM HR Timer Start.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_hrtimer_start(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm = substream->runtime->private_data;

    BTPCM_INFO("period=%lld\n", dpcm->period_time.tv64);
    dpcm->base_time = hrtimer_cb_get_time(&dpcm->timer);
    hrtimer_start(&dpcm->timer, dpcm->period_time, HRTIMER_MODE_REL);
    atomic_set(&dpcm->running, 1);
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_stop
 **
 ** Description      ALSA PCM HR Timer Stop.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_hrtimer_stop(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm = substream->runtime->private_data;

    BTPCM_INFO("\n");

    atomic_set(&dpcm->running, 0);
    hrtimer_cancel(&dpcm->timer);
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_sync
 **
 ** Description      ALSA PCM HR Timer Sync.
 **
 ** Returns          None
 **
 *******************************************************************************/
static inline void btpcm_alsa_hrtimer_sync(struct btpcm_alsa_hrtimer_pcm *dpcm)
{
    tasklet_kill(&dpcm->tasklet);
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_pointer
 **
 ** Description      ALSA PCM HR Timer Pointer.
 **
 ** Returns          Nb PCM Frames
 **
 *******************************************************************************/
static snd_pcm_uframes_t btpcm_alsa_hrtimer_pointer(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct btpcm_alsa_hrtimer_pcm *dpcm = runtime->private_data;
    u64 delta;
    u32 pos;

    delta = ktime_us_delta(hrtimer_cb_get_time(&dpcm->timer), dpcm->base_time);
    delta = div_u64(delta * runtime->rate + 999999, 1000000);
    div_u64_rem(delta, runtime->buffer_size, &pos);

    return pos;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_prepare
 **
 ** Description      ALSA PCM HR Timer Prepare.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_hrtimer_prepare(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct btpcm_alsa_hrtimer_pcm *dpcm = runtime->private_data;
    unsigned int period, rate;
    long sec;
    unsigned long nsecs;

    btpcm_alsa_hrtimer_sync(dpcm);
    period = runtime->period_size;
    rate = runtime->rate;
    sec = period / rate;
    period %= rate;
    nsecs = div_u64((u64)period * 1000000000UL + rate - 1, rate);
    dpcm->period_time = ktime_set(sec, nsecs);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_create
 **
 ** Description      ALSA PCM HR Timer Create.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_hrtimer_create(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm;

    dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
    if (!dpcm)
        return -ENOMEM;
    substream->runtime->private_data = dpcm;
    hrtimer_init(&dpcm->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dpcm->timer.function = btpcm_alsa_hrtimer_callback;
    dpcm->substream = substream;
    atomic_set(&dpcm->running, 0);
    tasklet_init(&dpcm->tasklet, btpcm_alsa_hrtimer_pcm_elapsed,
             (unsigned long)dpcm);
    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_hrtimer_free
 **
 ** Description      ALSA PCM HR Timer Free.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_alsa_hrtimer_free(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa_hrtimer_pcm *dpcm = substream->runtime->private_data;
    btpcm_alsa_hrtimer_sync(dpcm);
    kfree(dpcm);
}

/* ALSA Timer operations */
static struct btpcm_alsa_timer_ops btpcm_alsa_hrtimer_ops = {
    .create =   btpcm_alsa_hrtimer_create,
    .free =     btpcm_alsa_hrtimer_free,
    .prepare =  btpcm_alsa_hrtimer_prepare,
    .start =    btpcm_alsa_hrtimer_start,
    .stop =     btpcm_alsa_hrtimer_stop,
    .pointer =  btpcm_alsa_hrtimer_pointer,
};

/*
 * PCM interface used (by ALSA)
 */

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_trigger
 **
 ** Description      ALSA PCM Trigger.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    struct btpcm_alsa *p_btpcm = snd_pcm_substream_chip(substream);

    BTPCM_INFO("strm->nb=%d cmd=%d\n", substream->number, cmd);

    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
    case SNDRV_PCM_TRIGGER_RESUME:
        return p_btpcm->timer_ops->start(substream);
    case SNDRV_PCM_TRIGGER_STOP:
    case SNDRV_PCM_TRIGGER_SUSPEND:
        return p_btpcm->timer_ops->stop(substream);
    }
    return -EINVAL;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_prepare
 **
 ** Description      ALSA PCM Prepare.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa *p_btpcm = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;
    unsigned int bps;

    bps = runtime->rate * runtime->channels;
    bps *= snd_pcm_format_width(runtime->format);
    bps /= 8;

    BTPCM_INFO("strm->nb=%d rate=%dbps freq=%dHz\n", substream->number,
            (int)bps, (int)(bps/BTPCM_FRAME_SIZE));

    return p_btpcm->timer_ops->prepare(substream);
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_prepare
 **
 ** Description      ALSA PCM Pointer.
 **
 ** Returns          Nb PCM Frames
 **
 *******************************************************************************/
static snd_pcm_uframes_t btpcm_alsa_pcm_pointer(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa *p_btpcm = snd_pcm_substream_chip(substream);

    return p_btpcm->timer_ops->pointer(substream);
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_hw_params
 **
 ** Description      ALSA PCM HW Parameters.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_hw_params(struct snd_pcm_substream *substream,
                   struct snd_pcm_hw_params *hw_params)
{
    BTPCM_INFO("strm->nb=%d\n", substream->number);

    /* runtime->dma_bytes has to be set manually to allow mmap */
    substream->runtime->dma_bytes = params_buffer_bytes(hw_params);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_hw_free
 **
 ** Description      ALSA PCM Free HW Parameters.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_hw_free(struct snd_pcm_substream *substream)
{
    BTPCM_INFO("strm->nb=%d\n", substream->number);

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_open
 **
 ** Description      ALSA PCM Open.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_open(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa *p_btpcm = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;
    int err;

    BTPCM_INFO("strm->nb=%d\n", substream->number);

    p_btpcm->timer_ops = &btpcm_alsa_hrtimer_ops;

    err = p_btpcm->timer_ops->create(substream);
    if (err < 0)
        return err;

    runtime->hw = p_btpcm->pcm_hw;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_close
 **
 ** Description      ALSA PCM Close.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_close(struct snd_pcm_substream *substream)
{
    struct btpcm_alsa *p_btpcm = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;

    BTPCM_INFO("strm->nb=%d\n", substream->number);

    p_btpcm->timer_ops->free(substream);

    if (runtime->dma_area != NULL)
    {
        kfree(runtime->dma_area);
    }

    return 0;
}

#if defined(BTPCM_DBGFLAGS) && (BTPCM_DBGFLAGS & BTPCM_DBGFLAGS_INFO)
/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_ioctl_desc
 **
 ** Description      Get ALSA PCM IOCTL Description.
 **
 ** Returns          IOCTL DEscription
 **
 *******************************************************************************/
static char *btpcm_alsa_pcm_ioctl_desc(unsigned int cmd)
{
    switch(cmd)
    {
    case SNDRV_PCM_IOCTL1_RESET:
        return "Reset";
    case SNDRV_PCM_IOCTL1_INFO:
        return "Info";
    case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
        return "ChannelInfo";
    case SNDRV_PCM_IOCTL1_GSTATE:
        return "GState";
    case SNDRV_PCM_IOCTL1_FIFO_SIZE:
        return "FifoSize";
    default:
        return "Unknown";
    }
}
#endif

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_ioctl
 **
 ** Description      ALSA PCM IOCTL.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_ioctl(struct snd_pcm_substream *substream , unsigned int cmd, void *arg)
{
    int err;

    BTPCM_INFO("strm->nb=%d cmd=%s (%d)\n", substream->number,
            btpcm_alsa_pcm_ioctl_desc(cmd), cmd);

    /* Call the default ALSA IOCTL Command Handler */
    err = snd_pcm_lib_ioctl(substream, cmd, arg);
    if (err < 0)
    {
        BTPCM_ERR("snd_pcm_lib_ioctl failed err=%d\n", err);
    }

    return err;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_copy
 **
 ** Description      ALSA PCM Copy.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_copy(struct snd_pcm_substream *substream,
              int channel, snd_pcm_uframes_t pos,
              void __user *dst, snd_pcm_uframes_t count)
{
    BTPCM_DBG("strm->nb=%d channel=%d pos=%lu count=%lu\n",
            substream->number, channel, pos, count);

    /* Write the Audio data (from User Space) into the ring Buffer */
    btpcm_alsa_rb_write(substream->number, dst, count * BTPCM_FRAME_SIZE);

    return count;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_silence
 **
 ** Description      ALSA PCM Silence.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_pcm_silence(struct snd_pcm_substream *substream,
                 int channel, snd_pcm_uframes_t pos,
                 snd_pcm_uframes_t count)
{
    BTPCM_DBG("strm->nb=%d channel=%d pos=%lu count=%lu\n",
            substream->number, channel, pos, count);

    return 0; /* do nothing */
}

/* ALSA PCM Operations */
static struct snd_pcm_ops btpcm_alsa_ops = {
    .open =         btpcm_alsa_pcm_open,
    .close =        btpcm_alsa_pcm_close,
    .ioctl =        btpcm_alsa_pcm_ioctl,
    .hw_params =    btpcm_alsa_pcm_hw_params,
    .hw_free =      btpcm_alsa_pcm_hw_free,
    .prepare =      btpcm_alsa_pcm_prepare,
    .trigger =      btpcm_alsa_pcm_trigger,
    .pointer =      btpcm_alsa_pcm_pointer,
    .copy =         btpcm_alsa_pcm_copy,
    .silence =      btpcm_alsa_pcm_silence,
};


/*
 * Interface with BTPCM Core
 */

/*******************************************************************************
 **
 ** Function         btpcm_alsa_init
 **
 ** Description      BTPCM Tone Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_init(void)
{
    int err;

    BTPCM_INFO("\n");

    /* Initialize High Resolution Timer */
    err = btpcm_hrtimer_init();
    if (err < 0)
    {
        BTPCM_ERR("btpcm_hrtimer_init failed\n");
        return err;
    }

    /* Create a Virtual audio Card */
    err = btpcm_alsa_card_create(0);
    if (err < 0)
    {
        BTPCM_ERR("Unable to initiate BTPCM ALSA Driver\n");
        return err;
    }

    /* Initialize Control Block */
    memset(&btpcm_alsa_cb, 0, sizeof(btpcm_alsa_cb));

    return err;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_exit
 **
 ** Description      BTPCM tone Exit function.
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_alsa_exit(void)
{
    btpcm_alsa_unregister_all();            /* Stop/free every stream */

    btpcm_hrtimer_exit();                   /* Stop High Resolution Timer */
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_open
 **
 ** Description      BTPCM Tone Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_open(int stream)
{
    struct btpcm_alsa_ccb *p_ccb;

    BTPCM_ERR("stream=%d\n", stream);

    /* Check the stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return -ENOSR;
    }

    /* Get reference of the stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Check if this stream is already opened */
    if(p_ccb->opened)
    {
        BTPCM_ERR("stream=%d already opened\n", stream);
        return -ENOSR;
    }

    /* Erase this CCB to be safe */
    memset(p_ccb, 0, sizeof(*p_ccb));

    /* Allocate the Ring Buffer */
    p_ccb->rb.p_buf = kmalloc(BTPCM_ALSA_BUFFER_SIZE, GFP_KERNEL);
    if (p_ccb->rb.p_buf == NULL)
    {
        BTPCM_ERR("No memory\n");
        return -ENOMEM;
    }

    /* Initialize the Ring buffer */
    p_ccb->rb.in = 0;
    p_ccb->rb.out = 0;
    p_ccb->rb.count = 0;
    /* Initialize the Ring Buffer's lock */
    spin_lock_init(&p_ccb->rb.lock);

    /* This stream in now opened */
    p_ccb->opened = 1;

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_close
 **
 ** Description      BTPCM Tone Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_close(int stream)
{
    struct btpcm_alsa_ccb *p_ccb;

    BTPCM_ERR("stream=%d\n", stream);

    /* Check the stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return -ENOSR;
    }

    /* Get reference of the stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    if(p_ccb->opened == 0)
    {
        BTPCM_ERR("stream=%d not opened\n", stream);
        return -ENOSR;
    }

    /* Free the Ring buffer */
    kfree(p_ccb->rb.p_buf);

    /* This stream is now closed */
    p_ccb->opened = 0;

    /* Erase this CCB to be safe */
    memset(p_ccb, 0, sizeof(*p_ccb));

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_config
 **
 ** Description      BTPCM Tone Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_config(int stream, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
        void (*callback) (int stream, void *p_opaque, void *p_buf, int nb_pcm_frames))
{
    struct btpcm_alsa_ccb *p_ccb;

    BTPCM_DBG("stream=%d freq=%d nb_channel=%d bps=%d cback=%p\n",
            stream, frequency, nb_channel, bits_per_sample, callback);

    /* Check the stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return -ENOSR;
    }

    /* Check the callback parameter */
    if (callback == NULL)
    {
        BTPCM_ERR("Null Callback\n");
        return -EINVAL;
    }

    /* Check the p_opaque parameter */
    if (p_opaque == NULL)
    {
        BTPCM_ERR("Null p_opaque\n");
        return -EINVAL;
    }

    /* Check the frequency parameter */
    if ((frequency != 44100) && (frequency != 48000))
    {
        BTPCM_ERR("frequency=%d unsupported\n", frequency);
        return -EINVAL;
    }

    /* Check nb_channel parameter */
    if (nb_channel != 2)
    {
        BTPCM_ERR("nb_channel=%d unsupported\n", nb_channel);
        return -EINVAL;
    }

    /* Check bits_per_sample parameter */
    if (bits_per_sample != 16)
    {
        BTPCM_ERR("bits_per_sample=%d unsupported\n", bits_per_sample);
        return -EINVAL;
    }

    /* Get reference of the stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Check if the stream is already started */
    if (p_ccb->started)
    {
        BTPCM_ERR("stream=%d already started\n", stream);
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
 ** Function         btpcm_alsa_start
 **
 ** Description      BTPCM Tone Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_start(int stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    struct btpcm_alsa_ccb *p_ccb;
    uint64_t period_ns;
    int err = 0;

    BTPCM_INFO("stream=%d nb_pcm_frames=%d nb_pcm_packets=%d synchronization=%d\n",
            stream, nb_pcm_frames, nb_pcm_packets, synchronization);

    /* Check the stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return -ENOSR;
    }

    /* Check the Number of PCM and Packets requested */
    if ((nb_pcm_frames == 0)|| (nb_pcm_packets == 0))
    {
        BTPCM_ERR("Bad nb_pcm_frames=%d nb_pcm_packets=%d\n", nb_pcm_frames, nb_pcm_packets);
        return -EINVAL;
    }

    /* Get reference of the stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Check if the stream is opened */
    if (p_ccb->opened == 0)
    {
        BTPCM_ERR("stream=%d not opened\n", stream);
        return -EINVAL;
    }

    /* Check if the stream is already started */
    if (p_ccb->started)
    {
        BTPCM_ERR("stream=%d already started\n", stream);
        return -EINVAL;
    }

    /* Allocate a PCM buffer able to contain nb_frames samples * nb packet (stereo,16 bits) */
    p_ccb->p_buf = kmalloc(nb_pcm_frames * nb_pcm_packets * BTPCM_FRAME_SIZE, GFP_KERNEL);
    if (p_ccb->p_buf == NULL)
    {
        BTPCM_ERR("Unable to allocate buffer (size=%d)\n",
                (int)(nb_pcm_frames * nb_pcm_packets * BTPCM_FRAME_SIZE));
        return -ENOMEM;
    }

    /* If synchronization, we don't need timer */
    if (synchronization == 0)
    {
        /* Check that no timer allocated (it should not) */
        if (p_ccb->hr_timer == NULL)
        {
            /* Allocate a High Resolution timer */
            p_ccb->hr_timer =  btpcm_hrtimer_alloc(btpcm_alsa_pcm_hrtimer_callback,
                    (void *)(uintptr_t)stream);
            if (p_ccb->hr_timer == NULL)
            {
                BTPCM_ERR("No more timer\n");
                err = -ENOMEM;
                goto start_err;
            }
        }
        /* Calculate the timer period */
        period_ns = nb_pcm_frames * nb_pcm_packets;
        period_ns *= 1000; /* Milliseconds */
        period_ns *= 1000; /* Microseconds */
        period_ns *= 1000; /* Nanoseconds */
        /* 64 bits division cannot be used directly in Linux Kernel */
        do_div(period_ns, p_ccb->frequency);

        /* Save parameters */
        p_ccb->nb_frames = nb_pcm_frames;
        p_ccb->nb_packets = nb_pcm_packets;

        BTPCM_INFO("HR Timer_duration=%llu(ns) nb_frames=%d nb_packets=%d\n",
                period_ns, p_ccb->nb_frames, p_ccb->nb_packets);

        /* Start the High Resolution timer */
        err = btpcm_hrtimer_start(p_ccb->hr_timer, (unsigned long)period_ns);
        if (err < 0)
        {
            BTPCM_ERR("Unable to start timer\n");
            err = -EINVAL;
            goto start_err;
        }
    }
    else
    {
        BTPCM_ERR("Synchronization not implemented\n");
        err = -EINVAL;
        goto start_err;
    }

    /* The stream is now started */
    p_ccb->started = 1;

    return 0;

start_err:
    if (p_ccb)
    {
        if (p_ccb->p_buf)
        {
            kfree(p_ccb->p_buf);
            p_ccb->p_buf = NULL;
        }
        if (p_ccb->hr_timer)
        {
            btpcm_hrtimer_free(p_ccb->hr_timer);
            p_ccb->hr_timer = NULL;
        }
    }
    return err;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_stop
 **
 ** Description      BTPCM Tone Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int btpcm_alsa_stop(int stream)
{
    struct btpcm_alsa_ccb *p_ccb;
    int err;

    BTPCM_DBG("stream=%d\n", stream);

    /* Check stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return -ENOSR;
    }

    /* Get reference of the stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Check if the stream is opened */
    if (p_ccb->opened == 0)
    {
        BTPCM_ERR("stream=%d not opened\n", stream);
        return -EINVAL;
    }

    /* Check if the stream is Started */
    if (p_ccb->started == 0)
    {
        BTPCM_ERR("stream=%d not started\n", stream);
        return -EINVAL;
    }

    /* The stream is now stopped */
    p_ccb->started = 0;

    /* Stop the High Resolution timer */
    err = btpcm_hrtimer_stop(p_ccb->hr_timer);
    if (err < 0)
    {
        BTPCM_ERR("Unable to stop timer\n");
        return -EINVAL;
    }

    /* Free the Timer */
    btpcm_hrtimer_free(p_ccb->hr_timer);
    p_ccb->hr_timer = NULL;

    /* Free the PCM buffer */
    if (p_ccb->p_buf)
    {
        kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_synchronization
 **
 ** Description      BTPCM Tone Synchronization function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void btpcm_alsa_synchronization(int stream)
{
    BTPCM_ERR("Not Implemented\n");
}

/* BTPCM operation structure (used by BTUSB) */
const struct btpcm_op btpcm_operation =
{
    btpcm_alsa_init,
    btpcm_alsa_exit,
    btpcm_alsa_open,
    btpcm_alsa_close,
    btpcm_alsa_config,
    btpcm_alsa_start,
    btpcm_alsa_stop,
    btpcm_alsa_synchronization,
};

/*******************************************************************************
 **
 ** Function         btpcm_alsa_pcm_hrtimer_callback
 **
 ** Description      PCM Timer callback.
 **
 ** Returns          None
 **
 *******************************************************************************/
static void btpcm_alsa_pcm_hrtimer_callback(void *p_opaque)
{
    int stream = (int)(uintptr_t)p_opaque; /* Extrace stream nb from parameter */
    int nb_packets;
    int nb_frames;
    struct btpcm_alsa_ccb *p_ccb;

    /* Check stream value */
    if ((stream < 0) ||
        (stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return;
    }

    /* Get Stream reference */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* If this stream is not started */
    if (p_ccb->started == 0)
    {
        BTPCM_ERR("stream=%d stopped.\n", stream);
        /* Free the PCM buffer */
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

    /* If no PCM buffer */
    if (p_ccb->p_buf == NULL)
    {
        BTPCM_ERR("p_buff is NULL stream=%d\n", stream);
        p_ccb->started = 0;
        return;
    }

    /* If no callback function */
    if (p_ccb->callback == NULL)
    {
        BTPCM_ERR("callback is NULL stream=%d\n", stream);
        p_ccb->started = 0;
        if (p_ccb->p_buf)
            kfree(p_ccb->p_buf);
        p_ccb->p_buf = NULL;
        return;
    }

    nb_frames = 0;      /* No PCM frames for the moment */

    /* Try to read the requested number of packets */
    for (nb_packets = 0 ; nb_packets < p_ccb->nb_packets; nb_packets++)
    {
#ifdef BTPCM_ALSA_TONE
        /* for test purposes */
        btpcm_alsa_tone_read(p_ccb->p_buf + nb_frames * BTPCM_FRAME_SIZE,
                p_ccb->nb_frames * BTPCM_FRAME_SIZE);
        nb_frames += p_ccb->nb_frames;
#else
        /* If there is enough PCM frames available for one 'frame' */
        if (btpcm_alsa_rb_get_count(stream) >= p_ccb->nb_frames)
        {
            /* Read PCM samples */
            btpcm_alsa_rb_read(stream, p_ccb->p_buf + nb_frames * BTPCM_FRAME_SIZE,
                    p_ccb->nb_frames * BTPCM_FRAME_SIZE);
            /* Update number of PCM Frames */
            nb_frames += p_ccb->nb_frames;
        }
#endif
    }
    /* If PCM frames read */
    if (nb_frames)
    {
        BTPCM_DBG("nb_frames=%d\n", nb_frames);
        p_ccb->callback(stream, p_ccb->p_opaque, p_ccb->p_buf, nb_frames);
    }
}

/*
 * Ring Buffer management functions
 */

/*******************************************************************************
 **
 ** Function         btpcm_alsa_rb_write
 **
 ** Description      Write ALSA data (from User Space) in the Ring Buffer.
 **
 ** Returns          None
 **
 *******************************************************************************/
static void btpcm_alsa_rb_write(int stream, void __user *p_data, int nb_byte)
{
    struct btpcm_alsa_ccb *p_ccb;
    unsigned long irq_flags;

    BTPCM_DBG("stream=%d\n", stream);

    /* check that the incoming data is valid */
    if (unlikely(!access_ok(VERIFY_READ, (void *)p_data, nb_byte)))
    {
        BTPCM_ERR("buffer access verification failed\n");
        return;
    }

    /* Check The Stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return;
    }

    /* Get reference of the Stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* If the Stream is not opened (by BTUSB driver) */
    if (p_ccb->opened == 0)
    {
        BTPCM_ERR("stream=%d not opened\n", stream);
        return;
    }

    /* Sanity: Check Ring buffer */
    if (p_ccb->rb.p_buf == NULL)
    {
        BTPCM_ERR("no buffer\n");
        return;
    }

    /* Sanity: check that the ALSA buffer is not too big */
    if (nb_byte > BTPCM_ALSA_BUFFER_SIZE)
    {
        BTPCM_ERR("nb_byte=%d too big. Clip it to %d\n", nb_byte, BTPCM_ALSA_BUFFER_SIZE);
        nb_byte = BTPCM_ALSA_BUFFER_SIZE;
    }

    /* Lock access to Ring Buffer */
    SPIN_LOCK(&p_ccb->rb.lock, irq_flags);

    /* Check if one single copy can be done */
    if ((p_ccb->rb.in + nb_byte) <= BTPCM_ALSA_BUFFER_SIZE)
    {
        /* One copy only */
        copy_from_user(p_ccb->rb.p_buf + p_ccb->rb.in, p_data, nb_byte);
    }
    else
    {
        /* Two copies must be done*/
        copy_from_user(p_ccb->rb.p_buf + p_ccb->rb.in,
                p_data,
                BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.in);
        copy_from_user(p_ccb->rb.p_buf ,
                p_data + BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.in,
                nb_byte - (BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.in));
    }
    /* Update In index */
    p_ccb->rb.in += nb_byte;
    if (p_ccb->rb.in >= BTPCM_ALSA_BUFFER_SIZE)
        p_ccb->rb.in -= BTPCM_ALSA_BUFFER_SIZE;

    /* Unlock access to Ring Buffer */
    SPIN_UNLOCK(&p_ccb->rb.lock, irq_flags);
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_rb_get_count
 **
 ** Description      Returns the number of bytes available (for read) in the Ring Buffer
 **
 ** Returns          None
 **
 *******************************************************************************/
static int btpcm_alsa_rb_get_count(int stream)
{
    struct btpcm_alsa_ccb *p_ccb;
    int count;
    unsigned long irq_flags;

    /* Check The Stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return 0;
    }

    /* Get reference of the Stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Sanity: Check Ring buffer */
    if (p_ccb->rb.p_buf == NULL)
    {
        BTPCM_ERR("no buffer\n");
        return 0;
    }

    /* Lock access to Ring Buffer */
    SPIN_LOCK(&p_ccb->rb.lock, irq_flags);

    if (p_ccb->rb.in == p_ccb->rb.out)
    {
        count = 0;
    }
    else if (p_ccb->rb.in > p_ccb->rb.out)
    {
        count = p_ccb->rb.in - p_ccb->rb.out;
    }
    else
    {
        count = BTPCM_ALSA_BUFFER_SIZE - (p_ccb->rb.out - p_ccb->rb.in);
    }

    /* Unlock access to Ring Buffer */
    SPIN_UNLOCK(&p_ccb->rb.lock, irq_flags);

    return count;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_rb_read
 **
 ** Description      Read bytes from the Ring Buffer
 **
 ** Returns          None
 **
 *******************************************************************************/
static void btpcm_alsa_rb_read(int stream, void *p_data, int nb_byte)
{
    struct btpcm_alsa_ccb *p_ccb;
    unsigned long irq_flags;

    /* Check The Stream parameter */
    if ((stream < 0) ||
        (stream >= BTPCM_ALSA_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", stream);
        return;
    }

    /* Get reference of the Stream */
    p_ccb = &btpcm_alsa_cb.ccb[stream];

    /* Check if the stream is opened */
    if (p_ccb->opened == 0)
    {
        BTPCM_ERR("stream=%d not opened\n", stream);
        return;
    }

    /* Check if the stream is started */
    if (p_ccb->started == 0)
    {
        BTPCM_ERR("stream=%d not started\n", stream);
        return;
    }

    /* Sanity: Check Ring Buffer */
    if (p_ccb->rb.p_buf == NULL)
    {
        BTPCM_ERR("no buffer\n");
        return;
    }

    /* Sanity: check that the read size is not too big */
    if (nb_byte > BTPCM_ALSA_BUFFER_SIZE)
    {
        BTPCM_ERR("nb_byte=%d too big. Clip it to %d\n", nb_byte, BTPCM_ALSA_BUFFER_SIZE);
        nb_byte = BTPCM_ALSA_BUFFER_SIZE;
    }

    /* Lock access to Ring Buffer */
    SPIN_LOCK(&p_ccb->rb.lock, irq_flags);

    /* Check if one single copy can be done */
    if ((p_ccb->rb.out + nb_byte) <= BTPCM_ALSA_BUFFER_SIZE)
    {
        /* One copy (from Ring Buffer to buffer) */
        memcpy(p_data, p_ccb->rb.p_buf + p_ccb->rb.out, nb_byte);
    }
    else
    {
        /* One copies (from Ring Buffer to buffer) */
        memcpy(p_data,
               p_ccb->rb.p_buf + p_ccb->rb.out,
               BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.out);
        memcpy(p_data + BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.out,
               p_ccb->rb.p_buf,
               nb_byte - (BTPCM_ALSA_BUFFER_SIZE - p_ccb->rb.out));
    }

    /* Update Out index */
    p_ccb->rb.out += nb_byte;
    if (p_ccb->rb.out >= BTPCM_ALSA_BUFFER_SIZE)
        p_ccb->rb.out -= BTPCM_ALSA_BUFFER_SIZE;

    /* Unlock access to Ring Buffer */
    SPIN_UNLOCK(&p_ccb->rb.lock, irq_flags);
}

/*
 * Tone simulation (for debug)
 */
#ifdef BTPCM_ALSA_TONE
static const short sinwaves[64] =
{
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488,
         0,    488,    957,   1389,   1768,  2079,  2310,  2452,
      2500,   2452,   2310,   2079,   1768,  1389,   957,   488,
         0,   -488,   -957,  -1389,  -1768, -2079, -2310, -2452,
     -2500,  -2452,  -2310,  -2079,  -1768, -1389,  -957,  -488
};

/*******************************************************************************
 **
 ** Function         btpcm_alsa_tone_read
 **
 ** Description      Fill up a PCM buffer with a Sinux
 **
 ** Returns          void
 **
 *******************************************************************************/
static void btpcm_alsa_tone_read(short *p_buffer, int nb_bytes)
{
    int index;
    static int sinus_index = 0;

    /* Generate a standard PCM stereo interlaced sinewave */
    for (index = 0; index < (nb_bytes / 4); index++)
    {
        p_buffer[index * 2] = sinwaves[sinus_index % 64];
        p_buffer[index * 2 + 1] = sinwaves[sinus_index % 64];
        sinus_index++;
    }
}
#endif

/*******************************************************************************
 **
 ** Function         btpcm_alsa_card_create
 **
 ** Description      BTPCM ALSA fake probe function.
 **
 ** Returns          None
 **
 *******************************************************************************/
static int btpcm_alsa_card_create(int device)
{
    struct snd_card *card;
    struct btpcm_alsa *p_btpcm;
    int err;
    struct snd_pcm *pcm;
    int playback_count = 1;
    int capture_count = 0;
    char card_name[BTPCM_ALSA_CARD_DESC_MAX];

    BTPCM_INFO("device=%d\n", device);

    /* Create a Sound Card */
    snprintf(card_name, sizeof(card_name), BTPCM_ALSA_CARD_DESC, device);
    err = snd_card_create(btpcm_alsa_card_table[device], card_name, THIS_MODULE,
                  sizeof(struct btpcm_alsa), &card);
    if (err < 0)
    {
        BTPCM_ERR("snd_card_create failed s=%d\n", err);
        return err;
    }

    /* Save Card data */
    p_btpcm = card->private_data;
    p_btpcm->card = card;
    p_btpcm->pcm_hw = btpcm_alsa_hw;

    strcpy(card->driver, BTPCM_ALSA_DRV_DESC);
    strcpy(card->shortname, BTPCM_ALSA_DRV_DESC);
    sprintf(card->longname, "BTPCM_ALSA_DRV_DESC %i", device + 1);

    /* Create one Playback PCM instance */
    err = snd_pcm_new(p_btpcm->card, "BTPCM ALSA", device,
            playback_count, capture_count, &pcm);
    if (err < 0)
    {
        BTPCM_ERR("snd_pcm_new failed s=%d\n", err);
        return err;
    }
    p_btpcm->pcm = pcm;

    /* Set Playback operations */
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &btpcm_alsa_ops);

    pcm->private_data = p_btpcm;
    pcm->info_flags = 0;
    strcpy(pcm->name, BTPCM_ALSA_PCM_PB_DESC);

    /* Register this card into ALSA */
    err = snd_card_register(card);
    if (err < 0)
    {
        BTPCM_ERR("snd_card_register failed s=%d\n", err);
        snd_card_free(card);
        return err;
    }

    return 0;
}

/*******************************************************************************
 **
 ** Function         btpcm_alsa_unregister_all
 **
 ** Description      BTPCM ALSA global deregistration function.
 **
 ** Returns          None
 **
 *******************************************************************************/
static void btpcm_alsa_unregister_all(void)
{
    int stream;

    BTPCM_INFO("\n");

    for (stream = 0 ; stream < BTPCM_ALSA_NB_STREAM_MAX ; stream++)
    {
        if (btpcm_alsa_cb.ccb[stream].opened)
        {
            if (btpcm_alsa_cb.ccb[stream].started)
            {
                btpcm_alsa_stop(stream);
            }
            btpcm_alsa_close(stream);
        }
    }
}

