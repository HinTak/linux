/*
 *
 * btpcm_core.h
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

#include <linux/module.h>

#include <btpcm.h>
#include <btpcm_api.h>

/*
 * Definitions
 */
#define BTPCM_CCB_STATE_FREE        0x00
#define BTPCM_CCB_STATE_OPENED      0x01
#define BTPCM_CCB_STATE_CONFIGURED  0x02
#define BTPCM_CCB_STATE_STARTED     0x03

/* BTPCM Channel Control Block */
struct btpcm_ccb_t
{
    void (*callback) (int stream, void *p_opaque, void *p_buf, int nb_pcm_frames);
    int frequency;          /* Configured Frequency */
    int nb_channel;         /* Configured Number of Channels */
    int bit_per_sample;     /* Configured Number of bits per Sample */
    int nb_frames;          /* Configured Number of PCM Frames */
    int state;              /* free/opened/started */
    int timer;
};

/* BTPCM Control Block */
struct btpcm_cb_t
{
    struct btpcm_ccb_t ccb[BTPCM_NB_STREAM_MAX];
};

/* BTPCM Operation (exported by another C file depending on compile option) */
extern const struct btpcm_op btpcm_operation;

/*
 * Global variables
 */
struct btpcm_cb_t btpcm_cb; /* BTPCM control block */

/*
 * Local functions
 */
static void btpcm_callback(int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames);
static int __init btpcm_module_init(void);
static void __exit btpcm_module_exit(void);

/*******************************************************************************
 **
 ** Function         btpcm_open
 **
 ** Description      BTPCM Stream Open function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_open(int pcm_stream)
{
    int err;
    struct btpcm_ccb_t *p_btpcm;

    BTPCM_DBG("pcm_stream=%d\n", pcm_stream);

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    /* Get reference to PCM CCB */
    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_FREE:
        break;

    case BTPCM_CCB_STATE_OPENED:
    case BTPCM_CCB_STATE_CONFIGURED:
    case BTPCM_CCB_STATE_STARTED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d", p_btpcm->state, pcm_stream);
        return -EINVAL;
    }

    if (btpcm_operation.open)
    {
        err = btpcm_operation.open(pcm_stream);
        if (err < 0)
        {
            return err;
        }
    }

    memset(p_btpcm, 0, sizeof(*p_btpcm));

    /* Mark this pcm_stream as opened */
    p_btpcm->state = BTPCM_CCB_STATE_OPENED;

    return 0;
}
EXPORT_SYMBOL(btpcm_open);

/*******************************************************************************
 **
 ** Function         btpcm_close
 **
 ** Description      BTPCM Stream Close function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_close(int pcm_stream)
{
    int err;
    struct btpcm_ccb_t *p_btpcm;

    BTPCM_DBG("pcm_stream=%d\n", pcm_stream);

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    /* Get reference to PCM CCB */
    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_FREE:
        BTPCM_WRN("pcm_stream=%d already closed", pcm_stream);
        /* No break on purpose */

    case BTPCM_CCB_STATE_OPENED:
    case BTPCM_CCB_STATE_CONFIGURED:
        break;

    case BTPCM_CCB_STATE_STARTED:
        btpcm_stop(pcm_stream);
        break;

    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d", p_btpcm->state, pcm_stream);
        return -EINVAL;
    }

    if (btpcm_operation.close)
    {
        err = btpcm_operation.close(pcm_stream);
        if (err < 0)
        {
            return err;
        }
    }

    /* Mark this pcm_stream as free */
    p_btpcm->state = BTPCM_CCB_STATE_FREE;

    return 0;
}
EXPORT_SYMBOL(btpcm_close);


/*******************************************************************************
 **
 ** Function         btpcm_config
 **
 ** Description      BTPCM Stream Configuration function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_config(int pcm_stream, void *p_opaque, int frequency, int nb_channel, int bits_per_sample,
        void (*callback) (int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames))
{
    int err;
    struct btpcm_ccb_t *p_btpcm;

    BTPCM_INFO("pcm_stream=%d frequency=%d nb_channel=%d bits_per_sample=%d",
            pcm_stream, frequency, nb_channel, bits_per_sample);

    if (p_opaque == NULL)
    {
        BTPCM_ERR("Null p_opaque\n");
        return -EINVAL;
    }

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    if (callback == NULL)
    {
        BTPCM_ERR("No Callback Function\n");
        return -EINVAL;
    }

    /* Stereo supported only (for the moment) */
    if (nb_channel != 2)
    {
        BTPCM_ERR("Unsupported PCM NbChannel=%d\n", nb_channel);
        return -EINVAL;
    }

    /* 16 bits per sample supported only (for the moment) */
    if (bits_per_sample != 16)
    {
        BTPCM_ERR("Unsupported PCM BitsPerSamples=%d\n", bits_per_sample);
        return -EINVAL;
    }

    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_OPENED:
        /* no break on purpose */
    case BTPCM_CCB_STATE_CONFIGURED:
        break;

    case BTPCM_CCB_STATE_FREE:
    case BTPCM_CCB_STATE_STARTED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d\n", p_btpcm->state, pcm_stream);
        return -EINVAL;
    }

    if (btpcm_operation.config)
    {
        err = btpcm_operation.config(pcm_stream, p_opaque, frequency, nb_channel, bits_per_sample, btpcm_callback);
        if (err < 0)
        {
            /* Mark this pcm_stream as opened */
            p_btpcm->state = BTPCM_CCB_STATE_OPENED;
            return err;
        }
    }

    /* Mark this pcm_stream as configured */
    p_btpcm->state = BTPCM_CCB_STATE_CONFIGURED;

    /* Save the CCB parameters */
    p_btpcm->bit_per_sample = bits_per_sample;
    p_btpcm->frequency = frequency;
    p_btpcm->nb_channel = nb_channel;
    p_btpcm->callback = callback;
    p_btpcm->nb_frames = 0;

    return 0;
}
EXPORT_SYMBOL(btpcm_config);

/*******************************************************************************
 **
 ** Function         btpcm_set_shm_addr
 **
 ** Description      BTPCM function to receive PCM buffer addresses from
 **                  ALSA Lib.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_set_shm_addr(unsigned int base_addr, unsigned int access_point_addr, unsigned int buff_size, unsigned int frame_size_addr, unsigned enable_m_addr)
{
    int err;

    BTPCM_INFO("base_addr=%x access_point_addr=%x buff_size=%x frame_size_add=%x enable_m_addr = %x\n",
            base_addr, access_point_addr, buff_size, frame_size_addr, enable_m_addr);

    if (btpcm_operation.set_shm_addr)
    {
        err = btpcm_operation.set_shm_addr(base_addr, access_point_addr, buff_size, frame_size_addr, enable_m_addr);
        if (err < 0)
        {
            return err;
        }
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_set_shm_addr);

/*******************************************************************************
 **
 ** Function         btpcm_start
 **
 ** Description      BTPCM Stream Start function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_start(int pcm_stream, int nb_pcm_frames, int nb_pcm_packets, int synchronization)
{
    int err;
    struct btpcm_ccb_t *p_btpcm;

    BTPCM_INFO("pcm_stream=%d nb_pcm_frames=%d nb_pcm_packets=%d synchronization=%d\n",
            pcm_stream, nb_pcm_frames, nb_pcm_packets, synchronization);

    /* Check the pcm_stream */
    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_CONFIGURED:
        /* Mark this pcm_stream as started */
        p_btpcm->nb_frames = nb_pcm_frames;
        p_btpcm->state = BTPCM_CCB_STATE_STARTED;
        break;

    case BTPCM_CCB_STATE_FREE:
    case BTPCM_CCB_STATE_OPENED:
    case BTPCM_CCB_STATE_STARTED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d\n", p_btpcm->state, pcm_stream);
        return -EINVAL;
    }

    if (btpcm_operation.start)
    {
        err = btpcm_operation.start(pcm_stream, nb_pcm_frames, nb_pcm_packets, synchronization);
        if (err < 0)
        {
            BTPCM_ERR("Start op failed err=%d\n", err);
            /* Mark this pcm_stream as opened */
            p_btpcm->state = BTPCM_CCB_STATE_CONFIGURED;
            return err;
        }
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_start);

/*******************************************************************************
 **
 ** Function         btpcm_stop
 **
 ** Description      BTPCM Stream Stop function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
int btpcm_stop(int pcm_stream)
{
    int err;
    struct btpcm_ccb_t *p_btpcm;

    BTPCM_INFO("pcm_stream=%d", pcm_stream);

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return -ENOSR;
    }

    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_STARTED:
        /* Mark this pcm_stream as started */
        p_btpcm->state = BTPCM_CCB_STATE_CONFIGURED;
        break;

    case BTPCM_CCB_STATE_FREE:
    case BTPCM_CCB_STATE_CONFIGURED:
    case BTPCM_CCB_STATE_OPENED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d\n", p_btpcm->state, pcm_stream);
        return -EINVAL;
    }

    if (btpcm_operation.stop)
    {
        err = btpcm_operation.stop(pcm_stream);
        if (err < 0)
        {
            return err;
        }
    }

    return 0;
}
EXPORT_SYMBOL(btpcm_stop);

/*******************************************************************************
 **
 ** Function        btpcm_synchonization
 **
 ** Description     BTPCM Stream Synchonization function.
 **                 This function is called for Broadcast AV channel every time
 **                 a Broadcast Synchronization VSE is received (every 20ms)
 **                 It can be used by the PCM source to perform rate adaptation
 **                 to compensate the clock drift between the Host and Controller
 **                 Note that this function is subject to jitter (up to 20 ms)
 **
 ** Returns         void
 **
 *******************************************************************************/
void btpcm_synchonization(int pcm_stream)
{
    struct btpcm_ccb_t *p_btpcm;

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return;
    }

    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_STARTED:
        if (btpcm_operation.synchronization)
        {
            btpcm_operation.synchronization(pcm_stream);
        }
        break;

    case BTPCM_CCB_STATE_FREE:
    case BTPCM_CCB_STATE_CONFIGURED:
    case BTPCM_CCB_STATE_OPENED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d", p_btpcm->state, pcm_stream);
        break;
    }
}
EXPORT_SYMBOL(btpcm_synchonization);

/*******************************************************************************
 **
 ** Function        btpcm_callback
 **
 ** Description     BTPCM Stream Callback function.
 **
 ** Returns         Void
 **
 *******************************************************************************/
static void btpcm_callback(int pcm_stream, void *p_opaque, void *p_buf, int nb_pcm_frames)
{
    struct btpcm_ccb_t *p_btpcm;

    if ((pcm_stream < 0) ||
        (pcm_stream >= BTPCM_NB_STREAM_MAX))
    {
        BTPCM_ERR("Bad stream=%d\n", pcm_stream);
        return;
    }

    p_btpcm = &btpcm_cb.ccb[pcm_stream];

    switch (p_btpcm->state)
    {
    case BTPCM_CCB_STATE_STARTED:
        if (p_btpcm->callback)
        {
            /* Just pass the PCM samples to the callback (BTUSB) */
            p_btpcm->callback(pcm_stream, p_opaque, p_buf, nb_pcm_frames);
        }
        break;

    case BTPCM_CCB_STATE_FREE:
    case BTPCM_CCB_STATE_CONFIGURED:
    case BTPCM_CCB_STATE_OPENED:
    default:
        BTPCM_ERR("Bad state=%d for pcm_stream=%d", p_btpcm->state, pcm_stream);
        break;
    }
}

/*******************************************************************************
 **
 ** Function         btpcm_module_init
 **
 ** Description      BTPCM Module Init function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static int __init btpcm_module_init(void)
{
    int err;

    BTPCM_INFO("module inserted\n");

    if (btpcm_operation.init)
    {
        err = btpcm_operation.init();
        if (err < 0)
        {
            return err;
        }
    }

    memset (&btpcm_cb, 0, sizeof (btpcm_cb));

    return 0;
}
module_init(btpcm_module_init);

/*******************************************************************************
 **
 ** Function         btpcm_module_exit
 **
 ** Description      BTPCM Module Exit function.
 **
 ** Returns          Status
 **
 *******************************************************************************/
static void __exit btpcm_module_exit(void)
{
    if (btpcm_operation.exit)
    {
        btpcm_operation.exit();
    }

    BTPCM_INFO("module removed\n");
}
module_exit(btpcm_module_exit);


MODULE_DESCRIPTION("Broadcom Bluetooth PCM driver");
MODULE_LICENSE("GPL");
