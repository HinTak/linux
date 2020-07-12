/*
 *
 *  Generic Bluetooth USB driver
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/hci_core.h>

#define USE_PCM_READ 1

#ifdef USE_PCM_READ
#include "data_types.h"
#include "bt_types.h"
#include "bd.h"
#include "gki.h"
#include "gki_int.h"
#include "btpcm_api.h"
#include "btsbc_api.h"
#include "btusb_lite.h"
#endif

#include <linux/module.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>


#define VERSION "0.6"

static bool ignore_dga;
static bool ignore_csr;
static bool ignore_sniffer;
static bool disable_scofix;
static bool force_scofix;

static bool reset = 1;

static struct usb_driver btusb_driver;
static void btusb_av_close(struct hci_dev *hdev);

#define BTUSB_IGNORE		0x01
#define BTUSB_DIGIANSWER	0x02
#define BTUSB_CSR		0x04
#define BTUSB_SNIFFER		0x08
#define BTUSB_BCM92035		0x10
#define BTUSB_BROKEN_ISOC	0x20
#define BTUSB_WRONG_SCO_MTU	0x40
#define BTUSB_ATH3012		0x80


static struct usb_device_id btusb_table[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },

	/* Apple-specific (Broadcom) devices */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x05ac, 0xff, 0x01, 0x01) },

	/* Broadcom SoftSailing reporting vendor specific */
	{ USB_DEVICE(0x0a5c, 0x21e1) },

	/* Apple MacBookPro 7,1 */
	{ USB_DEVICE(0x05ac, 0x8213) },

	/* Apple iMac11,1 */
	{ USB_DEVICE(0x05ac, 0x8215) },

	/* Apple MacBookPro6,2 */
	{ USB_DEVICE(0x05ac, 0x8218) },

	/* Apple MacBookAir3,1, MacBookAir3,2 */
	{ USB_DEVICE(0x05ac, 0x821b) },

	/* Apple MacBookAir4,1 */
	{ USB_DEVICE(0x05ac, 0x821f) },

	/* Apple MacBookPro8,2 */
	{ USB_DEVICE(0x05ac, 0x821a) },

	/* Apple MacMini5,1 */
	{ USB_DEVICE(0x05ac, 0x8281) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800) },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_DEVICE(0x0c10, 0x0000) },

	/* Broadcom BCM20702A0 */
	{ USB_DEVICE(0x0b05, 0x17b5) },
	{ USB_DEVICE(0x04ca, 0x2003) },
	{ USB_DEVICE(0x0489, 0xe042) },
	{ USB_DEVICE(0x413c, 0x8197) },

	/* Foxconn - Hon Hai */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0489, 0xff, 0x01, 0x01) },

	/*Broadcom devices with vendor specific id */
	{ USB_VENDOR_AND_INTERFACE_INFO(0x0a5c, 0xff, 0x01, 0x01) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Atheros 3011 with sflash firmware */
	{ USB_DEVICE(0x0cf3, 0x3002), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0cf3, 0xe019), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x13d3, 0x3304), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0930, 0x0215), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe03d), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe027), .driver_info = BTUSB_IGNORE },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_DEVICE(0x03f0, 0x311d), .driver_info = BTUSB_IGNORE },

	/* Atheros 3012 with sflash firmware */
	{ USB_DEVICE(0x0cf3, 0x0036), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x817a), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3375), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3006), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3008), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3362), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0930, 0x0219), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe057), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3393), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe04e), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe056), .driver_info = BTUSB_ATH3012 },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe02c), .driver_info = BTUSB_IGNORE },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe03c), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0489, 0xe036), .driver_info = BTUSB_ATH3012 },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = BTUSB_BCM92035 },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 and 410 devices */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Asus WL-BTD202 device */
	{ USB_DEVICE(0x0b05, 0x1715), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622), .driver_info = BTUSB_BROKEN_ISOC },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002), .driver_info = BTUSB_SNIFFER },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002), .driver_info = BTUSB_SNIFFER },

	{ }	/* Terminating entry */
};

#define BTUSB_MAX_ISOC_FRAMES	10

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1
#define BTUSB_ISOC_RUNNING	2
#define BTUSB_SUSPENDING	3
#define BTUSB_DID_ISO_RESUME	4

struct btusb_data {
	struct hci_dev       *hdev;
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct usb_interface *isoc;

	spinlock_t lock;

	unsigned long flags;

	struct work_struct work;
	struct work_struct waker;

	struct usb_anchor tx_anchor;
	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;
	struct usb_anchor isoc_anchor;
	struct usb_anchor deferred;
	int tx_in_flight;
	spinlock_t txlock;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
	struct usb_endpoint_descriptor *isoc_tx_ep;
	struct usb_endpoint_descriptor *isoc_rx_ep;

	__u8 cmdreq_type;

	unsigned int sco_num;
	int isoc_altsetting;
	int suspend_count;

#ifdef USE_PCM_READ
	struct btusb_lite_cb lite_cb;
#endif
};


#ifdef USE_PCM_READ

#define BTUSB_DBG_MSG 1

#define BTUSB_DBG(fmt, ...) if (BTUSB_DBG_MSG) \
    printk(KERN_DEBUG "BTUSB %s: " fmt, __FUNCTION__, ##__VA_ARGS__)

#define BTUSB_INFO(fmt, ...) if (BTUSB_DBG_MSG) \
    printk(KERN_INFO "BTUSB %s: " fmt, __FUNCTION__, ##__VA_ARGS__)

#define BTUSB_ERR(fmt, ...) \
    printk(KERN_ERR "BTUSB %s: " fmt, __FUNCTION__, ##__VA_ARGS__)

#define UINT32_TO_BE_STREAM(p, u32) {*(p)++ = (UINT8)((u32) >> 24);  *(p)++ = (UINT8)((u32) >> 16); *(p)++ = (UINT8)((u32) >> 8); *(p)++ = (UINT8)(u32); }
#define UINT24_TO_BE_STREAM(p, u24) {*(p)++ = (UINT8)((u24) >> 16); *(p)++ = (UINT8)((u24) >> 8); *(p)++ = (UINT8)(u24);}
#define UINT16_TO_BE_STREAM(p, u16) {*(p)++ = (UINT8)((u16) >> 8); *(p)++ = (UINT8)(u16);}
#define UINT8_TO_BE_STREAM(p, u8)   {*(p)++ = (UINT8)(u8);}

#define BTUSB_LITE_AV_PCM_CHANNEL   0

/*  Codec (From BT Spec) */
#define A2D_MEDIA_TYPE_AUDIO        0x00

#define A2D_MEDIA_CT_SBC            0x00    /* SBC Codec */
#define A2D_MEDIA_CT_VEND           0xFF    /* Vendor specific */

/*  SBC Codec (From BT Spec) */
#define CODEC_SBC_LOSC              6

#define CODEC_SBC_FREQ_MASK         0xF0
#define CODEC_SBC_FREQ_48           0x10
#define CODEC_SBC_FREQ_44           0x20
#define CODEC_SBC_FREQ_32           0x40
#define CODEC_SBC_FREQ_16           0x80

#define CODEC_MODE_MASK             0x0F
#define CODEC_MODE_JOIN_STEREO      0x01
#define CODEC_MODE_STEREO           0x02
#define CODEC_MODE_DUAL             0x04
#define CODEC_MODE_MONO             0x08

#define CODEC_SBC_BLOCK_MASK        0xF0
#define CODEC_SBC_BLOCK_16          0x10
#define CODEC_SBC_BLOCK_12          0x20
#define CODEC_SBC_BLOCK_8           0x40
#define CODEC_SBC_BLOCK_4           0x80

#define CODEC_SBC_NBBAND_MASK       0x0C
#define CODEC_SBC_NBBAND_8          0x04
#define CODEC_SBC_NBBAND_4          0x08

#define CODEC_SBC_ALLOC_MASK        0x03
#define CODEC_SBC_ALLOC_LOUDNESS    0x01
#define CODEC_SBC_ALLOC_SNR         0x02

#define AV_MAX_A2DP_MTU     1008
#define REAL_CONNECTED_MTU     1021


struct btusb_av_sbc_param
{
    int frequency;
    unsigned char nb_blocks;
    unsigned char nb_subbands;
    unsigned char mode;
    unsigned char allocation;
    unsigned char bitpool_min;
    unsigned char bitpool_max;
};

#define SILENSE_PCM_BUF_SIZE    (2 * 128) /* 128 samples, Stereo */

//static int pcm0_mute;
static const unsigned short btusb_lite_silence_pcm[SILENSE_PCM_BUF_SIZE] = {0};

/* L2CAP */
int btusb_l2c_send(struct btusb_data *data, BT_HDR *p_msg, UINT16 local_cid)
{
    int idx;
    struct btusb_lite_l2c_cb *p_l2c;
    struct btusb_lite_l2c_ccb *p_l2c_ccb;
    UINT8 *p_data;

    /* Look for the first AV stream Started */
    p_l2c = &data->lite_cb.l2c;
    for (idx = 0, p_l2c_ccb = p_l2c->ccb ; idx < BTM_SYNC_INFO_NUM_STR ; idx++, p_l2c_ccb++)
    {
        if (p_l2c_ccb->local_cid == local_cid)
        {
            break;
        }
    }
    if (idx == BTM_SYNC_INFO_NUM_STR)
    {
        BTUSB_ERR("No L2C CCB found (lcid=0x%x)\n", local_cid);
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return -1;
    }

    /* Check if the Tx Quota has been reached for this channel */
    if (p_l2c_ccb->tx_pending >= p_l2c_ccb->link_xmit_quota)
    {
        BTUSB_ERR("Tx Quota reached (lcid=0x%x).\n", local_cid);
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return -1;
    }

    /* Sanity */
    if (p_msg->offset < BTUSB_LITE_L2CAP_HDR_SIZE)
    {
        BTUSB_ERR("offset too small=%d\n", p_msg->offset);
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return-1;
    }

    /* Decrement offset to add headers */
    p_msg->offset -= BTUSB_LITE_L2CAP_HDR_SIZE;

    /* Get address of the HCI Header */
    p_data = (UINT8 *)(p_msg + 1) + p_msg->offset;

    /* !!!!!!!! Write BT subsystem L2socket!!!!!!!!!!!!!!!! */

#if 0
    /* Write L2CAP Header (length field is SBC Frames + RTP/A2DP/Media Header) */
    p_data = btusb_lite_l2c_write_header(p_data, p_msg->len, p_l2c_ccb->remote_cid);

    /* Increment length */
    p_msg->len += BTUSB_LITE_L2CAP_HDR_SIZE;


    GKI_disable();      /* tx_pending field can be updated by another context */
    p_l2c_ccb->tx_pending++;            /* One more A2DP L2CAP packet pending */
    BTUSB_DBG("L2C Tx Pending=%d\n", p_l2c_ccb->tx_pending);
    GKI_enable();

    if (btusb_lite_hci_acl_send(p_dev, p_msg, p_l2c_ccb->handle) < 0)
    {
        GKI_disable();      /* tx_pending field can be updated by another context */
        p_l2c_ccb->tx_pending--;        /* Remove A2DP L2CAP packet pending */
        GKI_enable();
        return -1;
    }
#endif
    return 0;
}


/* AVDT */
struct btusb_lite_avdt_scb *btusb_avdt_scb_by_hdl(struct btusb_data *data, UINT8 handle)
{
    struct btusb_lite_avdt_scb *p_scb;
    UINT8 scb;

    p_scb = &data->lite_cb.avdt.scb[0];
    for(scb = 0; scb < AVDT_NUM_SEPS; scb++, p_scb++ )
    {
        if((p_scb->allocated) &&
           (p_scb->handle == handle))
            return(p_scb);
    }
    return(NULL);
}

static void btusb_avdt_free_ccb(struct btusb_data *data, struct btusb_lite_avdt_ccb *p_ccb_free)
{
    struct btusb_lite_avdt_ccb *p_ccb;
    UINT8 ccb;

    p_ccb = &data->lite_cb.avdt.ccb[0];
    for(ccb = 0; ccb < AVDT_NUM_LINKS; ccb++, p_ccb++)
    {
        if (p_ccb == p_ccb_free)
        {
            /* Sanity */
            if (!p_ccb_free->allocated)
            {
                BTUSB_ERR("CCB=%d was not allocated\n", ccb);
            }
            BTUSB_INFO("CCB=%d freed\n", ccb);
            p_ccb_free->allocated = FALSE;
            return;
        }
    }
}

static void btusb_avdt_free_scb(struct btusb_data *data, struct btusb_lite_avdt_scb *p_scb_free)
{
    struct btusb_lite_avdt_scb *p_scb;
    UINT8 scb;

    p_scb = &data->lite_cb.avdt.scb[0];
    for(scb = 0; scb < AVDT_NUM_SEPS; scb++, p_scb++)
    {
        if (p_scb == p_scb_free)
        {
            /* Sanity */
            if (!p_scb_free->allocated)
            {
                BTUSB_ERR("SCB=%d was not allocated\n", scb);
            }
            BTUSB_INFO("SCB=%d freed\n", scb);
            p_scb_free->allocated = FALSE;
            return;
        }
    }
}


UINT8 btusb_avdt_remove_scb(struct btusb_data *data, UINT8 handle, tAVDT_SCB_SYNC_INFO *p_scb_info)
{
    struct btusb_lite_avdt_scb *p_scb;

    if((p_scb = btusb_avdt_scb_by_hdl(data, handle)) == NULL)
    {
        BTUSB_ERR("No SCB for handle %d\n", handle);
        return AVDT_SYNC_FAILURE;
    }
    else
    {
        if (p_scb_info)
        {
            p_scb_info->handle = p_scb->handle;
            p_scb_info->media_seq = p_scb->media_seq;
        }
        /* Free CCB first */
        btusb_avdt_free_ccb(data, p_scb->p_ccb);
        /* Free SCB */
        btusb_avdt_free_scb(data, p_scb);

        return AVDT_SYNC_SUCCESS;
    }
}

static UINT8 *btusb_avdt_write_rtp_header(UINT8 *p_data, UINT8 m_pt, UINT16 seq_number,
        UINT32 timestamp, UINT32 ssrc)
{
    /* Write RTP Header */
    UINT8_TO_BE_STREAM(p_data, AVDT_MEDIA_OCTET1);  /* Version, Padding, Ext, CSRC */
    UINT8_TO_BE_STREAM(p_data, m_pt);               /* Marker & Packet Type */
    UINT16_TO_BE_STREAM(p_data, seq_number);        /* Sequence number */
    UINT32_TO_BE_STREAM(p_data, timestamp);         /* TimeStamp */
    UINT32_TO_BE_STREAM(p_data, ssrc);              /* SSRC */
    return p_data;
}

/*******************************************************************************
**
** Function         btusb_lite_avdt_allocate_scb
**
** Description      allocate SCB in lite stack
**
** Returns          pointer of SCB
**
*******************************************************************************/
static struct btusb_avdt_scb *btusb_lite_avdt_allocate_scb(struct btusb_data *p_dev)
{
    struct btusb_lite_avdt_scb *p_scb;
    UINT8 scb;

    p_scb = &p_dev->lite_cb.avdt.scb[0];
    for(scb = 0; scb < AVDT_NUM_SEPS; scb++, p_scb++)
    {
        if(!p_scb->allocated)
        {
            BTUSB_INFO("SCB=%d allocated\n", scb);
            memset(p_scb, 0, sizeof(struct btusb_lite_avdt_scb));
            p_scb->allocated = TRUE;
            return(p_scb);
        }
    }
    return(NULL);
}

/*******************************************************************************
**
** Function         btusb_lite_avdt_allocate_ccb
**
** Description      allocate CCB in lite stack
**
** Returns          pointer of CCB
**
*******************************************************************************/
static struct btusb_lite_avdt_ccb *btusb_lite_avdt_allocate_ccb(struct btusb_data *p_dev)
{
    struct btusb_lite_avdt_ccb *p_ccb = NULL;
    UINT8 ccb;
    p_ccb = &p_dev->lite_cb.avdt.ccb[0];
    for(ccb = 0; ccb < AVDT_NUM_LINKS; ccb++, p_ccb++)
    {
        if (!p_ccb->allocated)
        {
            memset(p_ccb, 0, sizeof(struct btusb_lite_avdt_ccb));
            p_ccb->allocated = TRUE;
            return(p_ccb);
        }
    }
    return(NULL);
}

unsigned char btusb_avdt_init_scb(struct btusb_data *p_dev/*, tAVDT_SCB_SYNC_INFO *p_scb_info*/)
{
    struct btusb_lite_avdt_scb *p_scb;
    struct btusb_lite_avdt_ccb *p_ccb;

    if((p_scb = btusb_lite_avdt_allocate_scb(p_dev)) == NULL)
    {
        return AVDT_SYNC_FAILURE;
    }
    else
    {
        if((p_ccb = btusb_lite_avdt_allocate_ccb(p_dev)) == NULL)
        {
            p_scb->allocated = FALSE;
            return AVDT_SYNC_FAILURE;
        }
        else
        {
            UINT8 BD_ADDR[BD_ADDR_LEN] = {0xF4, 0x7B, 0x5E, 0x7D, 0xD9, 0xBC};
            memcpy(p_ccb->peer_addr, BD_ADDR, BD_ADDR_LEN);
            /* Hardcoding to dummy values as these are not sued */
            p_ccb->lcid     = 0;//p_scb_info->local_cid;
            p_ccb->peer_mtu = 0;//p_scb_info->peer_mtu;
            p_scb->handle           = 1;//p_scb_info->handle;
            p_scb->mux_tsid_media   = 1;//p_scb_info->mux_tsid_media;
            p_scb->media_seq        = 0;//p_scb_info->media_seq;
            p_scb->p_ccb            = p_ccb;
        }
    }
    return AVDT_SYNC_SUCCESS;
}

void btusb_avdt_send(struct btusb_data *data, BT_HDR *p_msg, UINT8 avdt_handle,
        UINT8 m_pt, UINT8 option, UINT32 timestamp, int sbc_len)
{
    UINT8 *p_data;
    struct btusb_lite_avdt_scb *p_avdt_scb;
    struct btusb_lite_avdt_ccb *p_avdt_ccb;
    size_t sbc_frame_fragment_byte = 1;

    if (data == NULL)
    {
        BTUSB_ERR("p_dev is NULL\n");
        if (p_msg)
            GKI_freebuf(p_msg);
        return;
    }

    if (p_msg == NULL)
    {
        BTUSB_ERR("p_msg is NULL\n");
        return;
    }

    /* Find the AVDT SCB with this handle */
    p_avdt_scb = btusb_avdt_scb_by_hdl(data, avdt_handle);
    if (p_avdt_scb == NULL)
    {
        BTUSB_ERR("No AVDT SCB stream found\n");
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return;
    }

    /* Get the associated AVDT CCB */
    p_avdt_ccb = p_avdt_scb->p_ccb;
    if (p_avdt_ccb == NULL)
    {
        BTUSB_ERR("No AVDT CCB stream found\n");
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return;
    }

    /* Write the Media Payload Header if needed */
    if ((option & BTUSB_LITE_AVDT_OPT_NO_MPH) == 0)
    {
        if (p_msg->offset < BTUSB_LITE_MEDIA_SIZE)
        {
            BTUSB_ERR("Offset too small=%d for MediaPayloadHeader\n", p_msg->offset);
            GKI_freebuf(p_msg); /* Free this ACL buffer */
            return;
        }
        p_msg->offset -= BTUSB_LITE_MEDIA_SIZE;
        p_msg->len += BTUSB_LITE_MEDIA_SIZE;
        /* Get write address */
        p_data = (UINT8 *)(p_msg + 1) + p_msg->offset;
        /* Write Media Payload Header (Number of SBC Frames) */
        UINT8_TO_BE_STREAM(p_data, p_msg->layer_specific & A2D_SBC_HDR_NUM_MSK);
    }

    /* Write the SCMS content Protection Header if needed */
    if (p_avdt_scb->scms.enable)
    {
        if (p_msg->offset < BTUSB_LITE_SCMS_SIZE)
        {
            BTUSB_ERR("Offset too small=%d for CP Header\n", p_msg->offset);
            GKI_freebuf(p_msg); /* Free this ACL buffer */
            return;
        }
        p_msg->offset -= BTUSB_LITE_SCMS_SIZE;
        p_msg->len += BTUSB_LITE_SCMS_SIZE;
        /* Get write address */
        p_data = (UINT8 *)(p_msg + 1) + p_msg->offset;
        UINT8_TO_BE_STREAM(p_data, p_avdt_scb->scms.header);
    }

    /* Write the RTP Header if needed */
    if ((option & BTUSB_LITE_AVDT_OPT_NO_RTP) == 0)
    {
        if (p_msg->offset < BTUSB_LITE_RTP_SIZE)
        {
            BTUSB_ERR("Offset too small=%d for RTP Header\n", p_msg->offset);
            GKI_freebuf(p_msg); /* Free this ACL buffer */
            return;
        }
        p_msg->offset -= BTUSB_LITE_RTP_SIZE;
        p_msg->len += BTUSB_LITE_RTP_SIZE;
        /* Get write address */
        p_data = (UINT8 *)(p_msg + 1) + p_msg->offset;
        /* Write RTP Header */
        p_data = btusb_avdt_write_rtp_header(p_data, m_pt, p_avdt_scb->media_seq, timestamp, 1);
    }

    p_avdt_scb->media_seq++;    /* Increment Sequence number */

    /* Decrement the pointer location by 12 bytes(AVDTP header size) */
    p_data = p_data -BTUSB_LITE_RTP_SIZE;

    /* Send SBC encoded frame to BT Subsystem */
    l2cap_audio_send(p_data, sbc_len + BTUSB_LITE_RTP_SIZE+sbc_frame_fragment_byte);
    GKI_freebuf(p_msg); /* Free this ACL buffer */
    return;
}

static int btusb_av_parse_sbc_codec(struct btusb_av_sbc_param *p_sbc, UINT8 *p_codec)
{
    UINT8 byte;
    unsigned char codec_freq;
    unsigned char codec_blocks;
    unsigned char codec_subbands;
    unsigned char codec_mode;
    unsigned char codec_alloc;
    unsigned char bitpool_min;
    unsigned char bitpool_max;

    if (p_sbc == NULL)
    {
        BTUSB_ERR("p_sbc is NULL\n");
        return -1;
    }

    /* Extract LOSC */
    byte = *p_codec++;
    if (byte != CODEC_SBC_LOSC)
    {
        BTUSB_ERR("Bad SBC LOSC=%d", byte);
        return -1;
    }

    p_codec++; /* Ignore MT */

    /* Extract Codec Type */
    byte = *p_codec++;
    if (byte != A2D_MEDIA_CT_SBC)
    {
        BTUSB_ERR("Bad SBC codec type=%d", byte);
        return -1;
    }

    /* Extract Freq & Mode */
    byte = *p_codec++;
    codec_freq = byte & CODEC_SBC_FREQ_MASK;
    codec_mode = byte & CODEC_MODE_MASK;

    /* Extract NbBlock NbSubBand and Alloc Method */
    byte = *p_codec++;
    codec_blocks = byte & CODEC_SBC_BLOCK_MASK;
    codec_subbands = byte & CODEC_SBC_NBBAND_MASK;
    codec_alloc  = byte & CODEC_SBC_ALLOC_MASK;

    bitpool_min = *p_codec++;
    bitpool_max = *p_codec++;

    switch(codec_freq)
    {
    case CODEC_SBC_FREQ_48:
        BTUSB_INFO("SBC Freq=48K\n");
        p_sbc->frequency = 48000;
        break;
    case CODEC_SBC_FREQ_44:
        BTUSB_INFO("SBC Freq=44.1K\n");
        p_sbc->frequency = 44100;
        break;
    case CODEC_SBC_FREQ_32:
        BTUSB_INFO("SBC Freq=32K\n");
        p_sbc->frequency = 32000;
        break;
    case CODEC_SBC_FREQ_16:
        BTUSB_INFO("SBC Freq=16K\n");
        p_sbc->frequency = 16000;
        break;
    default:
        BTUSB_INFO("Bad SBC Freq=%d\n", codec_freq);
        return -1;
    }

    switch(codec_mode)
    {
    case CODEC_MODE_JOIN_STEREO:
        BTUSB_INFO("SBC Join Stereo\n");
        p_sbc->mode = CODEC_MODE_JOIN_STEREO;
        break;
    case CODEC_MODE_STEREO:
        BTUSB_INFO("SBC Stereo\n");
        p_sbc->mode = CODEC_MODE_STEREO;
        break;
    case CODEC_MODE_DUAL:
        BTUSB_INFO("SBC Dual\n");
        p_sbc->mode = CODEC_MODE_DUAL;
        break;
    case CODEC_MODE_MONO:
        BTUSB_INFO("SBC Mono\n");
        p_sbc->mode = CODEC_MODE_MONO;
        break;
    default:
        BTUSB_INFO("Bad SBC mode=%d\n", codec_mode);
        return -1;
    }

    switch(codec_blocks)
    {
    case CODEC_SBC_BLOCK_16:
        BTUSB_INFO("SBC Block=16\n");
        p_sbc->nb_blocks = 16;
        break;
    case CODEC_SBC_BLOCK_12:
        BTUSB_INFO("SBC Block=12\n");
        p_sbc->nb_blocks = 12;
        break;
    case CODEC_SBC_BLOCK_8:
        BTUSB_INFO("SBC Block=8\n");
        p_sbc->nb_blocks = 8;
        break;
    case CODEC_SBC_BLOCK_4:
        BTUSB_INFO("SBC Block=4\n");
        p_sbc->nb_blocks = 4;
        break;
    default:
        BTUSB_INFO("Bad SBC Block=%d\n", codec_blocks);
        return -1;
    }

    switch(codec_subbands)
    {
    case CODEC_SBC_NBBAND_8:
        BTUSB_INFO("SBC NbSubBand=8\n");
        p_sbc->nb_subbands = 8;
        break;
    case CODEC_SBC_NBBAND_4:
        BTUSB_INFO("SBC NbSubBand=4\n");
        p_sbc->nb_subbands = 4;
        break;
    default:
        BTUSB_INFO("Bad SBC NbSubBand=%d\n", codec_blocks);
        return -1;
    }

    switch(codec_alloc)
    {
    case CODEC_SBC_ALLOC_LOUDNESS:
        BTUSB_INFO("SBC Loudness\n");
        p_sbc->allocation = CODEC_SBC_ALLOC_LOUDNESS;
        break;
    case CODEC_SBC_ALLOC_SNR:
        BTUSB_INFO("SBC SNR\n");
        p_sbc->allocation = CODEC_SBC_ALLOC_SNR;
        break;
    default:
        BTUSB_INFO("Bad SBC AllocMethod=%d\n", codec_blocks);
        return -1;
    }

    BTUSB_INFO("BitpoolMin=%d BitpoolMax=%d\n", bitpool_min, bitpool_max);

    p_sbc->bitpool_min = bitpool_min;
    p_sbc->bitpool_max = bitpool_max;

    return 0;
}

static int btusb_sbc_get_bitpool(struct btusb_av_sbc_param *p_sbc_param, int target_bitrate)
{
    int nb_channels;
    int frame_length;
    int bitpool = p_sbc_param->bitpool_max + 1;
    int bitrate;

    /* Required number of channels */
    if (p_sbc_param->mode == CODEC_MODE_MONO)
        nb_channels = 1;
    else
        nb_channels = 2;

    target_bitrate *= 1000; /* Bitrate from app is in Kbps */

    do
    {
        bitpool--;  /* Reduce Bit Pool by one */

        /* Calculate common SBC Frame length */
        frame_length = 4 + (4 * p_sbc_param->nb_subbands * nb_channels) / 8;

        /* Add specific SBC Frame length (depending on mode) */
        switch(p_sbc_param->mode)
        {
        case CODEC_MODE_MONO:
        case CODEC_MODE_DUAL:
            frame_length += (p_sbc_param->nb_blocks * nb_channels * bitpool) / 8;
            break;
        case CODEC_MODE_JOIN_STEREO:
            frame_length += (p_sbc_param->nb_subbands + p_sbc_param->nb_blocks * bitpool) / 8;
            break;
        case CODEC_MODE_STEREO:
            frame_length += (p_sbc_param->nb_blocks * bitpool) / 8;
            break;
        }

        /* Calculate bit rate */
        bitrate = 8 * frame_length * p_sbc_param->frequency / p_sbc_param->nb_subbands / p_sbc_param->nb_blocks;

    } while (bitrate > target_bitrate); /* While bitrate is too big */

    BTUSB_INFO("final bitpool=%d frame_length=%d bitrate=%d\n", bitpool, frame_length, bitrate);

    return (int)bitpool;
}

static void btusb_av_send_packet(struct btusb_data *data, BT_HDR *p_msg, int sbc_len)
{
    struct btusb_lite_av_cb *p_av_cb;
    struct btusb_lite_av_scb *p_av_scb;
    int stream;
    int nb_started_streams;
    BT_HDR *p_msg_dup;

    if (!data || !p_msg)
    {
        BTUSB_ERR("Bad reference p_dev=%p p_msg=%p\n", data, p_msg);
        return;
    }

    /* Sanity */
    if (p_msg->len == 0)
    {
        BTUSB_ERR("Length is 0=%d\n", p_msg->len);
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return;
    }

    /* Get Reference on the AV Streams */
    p_av_cb = &((struct btusb_data *)data)->lite_cb.av;

    /* Update TimeStamp */
    p_av_cb->timestamp += p_msg->layer_specific * p_av_cb->encoder.pcm_frame_size;

    nb_started_streams = 0;
    /* Count how many AV stream are started */
    for (stream = 0, p_av_scb = p_av_cb->scb ; stream < BTA_AV_NUM_STRS ; stream++, p_av_scb++)
    {
        if (p_av_scb->started)
        {
            nb_started_streams++;   /* One more started stream */
        }
    }

    if (nb_started_streams == 0)
    {
        BTUSB_ERR("No Started AV stream found\n");
        GKI_freebuf(p_msg); /* Free this ACL buffer */
        return;
    }
    else if (nb_started_streams == 1)
    {
        p_msg_dup = NULL;
    }
    else
    {
        /*
         * Duplicate the AV packet
         */
        /* Get a buffer from the pool */
        p_msg_dup = (BT_HDR *)GKI_getbuf(sizeof(BT_HDR) + p_msg->offset + p_msg->offset);
        if(p_msg_dup)
        {
            if (unlikely(GKI_buffer_status(p_msg_dup) != BUF_STATUS_UNLINKED))
            {
                BTUSB_ERR("buffer != BUF_STATUS_UNLINKED 0x%p\n", p_msg_dup);
                p_msg_dup = NULL; /* Do not use this buffer */
            }
            if(p_msg_dup)
            {
                /* Duplicate all the data (Header, and payload */
                memcpy(p_msg_dup, p_msg, sizeof(BT_HDR) + p_msg->offset + p_msg->offset);
            }
        }
        if (nb_started_streams > 2)
        {
            BTUSB_ERR("nb_started_streams=%d force it to 2\n", nb_started_streams);
            nb_started_streams = 2;
        }
    }

    /* For every AV stream Started */
    for (stream = 0, p_av_scb = p_av_cb->scb ; stream < BTA_AV_NUM_STRS ; stream++, p_av_scb++)
    {
        if (p_av_scb->started)
        {
            if (p_msg)
            {
                /* Send the original packet to AVDT */
                btusb_avdt_send(data, p_msg, p_av_scb->avdt_handle,
                        p_av_cb->m_pt, p_av_cb->option, p_av_cb->timestamp, sbc_len);
                p_msg = NULL;
            }
            else if (p_msg_dup)
            {
                /* Send the duplicated packet to AVDT */
                btusb_avdt_send(data, p_msg_dup, p_av_scb->avdt_handle,
                        p_av_cb->m_pt, p_av_cb->option, p_av_cb->timestamp, sbc_len);
                p_msg_dup = NULL;
            }
            else
            {
                BTUSB_ERR("No AV data to send for AV stream=%d \n", stream);
            }
        }
    }
}

#define NETLINK_USER 31

struct sock *nl_sk = NULL;
int pid = 0;

void send_data_to_userspace(void *ptr, int msg_size)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	char *msg = ptr;
	int res;

	skb_out = nlmsg_new(msg_size, 0);
	if (!skb_out)
	{
		printk(KERN_ERR "Failed to allocate new skb\n");
		return;
	}

	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
	memcpy(nlmsg_data(nlh), msg, msg_size);
	res = nlmsg_unicast(nl_sk, skb_out, pid);
	if (res < 0) {
		printk(KERN_INFO "Error while sending bak to user\n");
		pid = -1;
	}
}

static void netlink_sock_nl_recv_msg(struct sk_buff *skb)
{

	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int msg_size;
	char *msg = "netlink_sock from kernel";
	int res;

	printk(KERN_INFO "Entering: %s\n", __FUNCTION__);

	msg_size = strlen(msg);

	nlh = (struct nlmsghdr *)skb->data;
	printk(KERN_INFO "Netlink received msg payload:%s\n", (char *)nlmsg_data(nlh));
	pid = nlh->nlmsg_pid; /*pid of sending process */

	skb_out = nlmsg_new(msg_size, 0);

	if (!skb_out)
	{

		printk(KERN_ERR "Failed to allocate new skb\n");
		return;

	}
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
	strncpy(nlmsg_data(nlh), msg, msg_size);

	res = nlmsg_unicast(nl_sk, skb_out, pid);

	if (res < 0)
		printk(KERN_INFO "Error while sending bak to user\n");
}

int netlink_sock_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.groups = 1,
		.input = netlink_sock_nl_recv_msg,
	};
	printk("Entering: %s\n", __FUNCTION__);
	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
	if (!nl_sk)
	{

		printk(KERN_ALERT "Error creating socket.\n");
		return -10;

	}

	return 0;
}

void netlink_sock_exit(void)
{

	printk(KERN_INFO "exiting netlink_sock module\n");
	netlink_kernel_release(nl_sk);
}

static void btusb_av_pcm_cback(int pcm_stream, void *p_dev, void *p_data,
        int nb_pcm_frames)
{
    struct btusb_lite_av_cb *p_av_cb;
    int pcm_frame_size_byte;
    void *p_dest;
    int written_enc_size;
    struct btusb_lite_encoder_ccb *p_encoder;

#if 0
    int num = 0;
    static int num1 = 0;
    void *ptr = NULL;
#endif

    if (p_dev == NULL)
    {
        BTUSB_ERR("Null _p_dev\n");
        return;
    }

    if (p_data == NULL)
    {
        BTUSB_ERR("Null p_data\n");
        return;
    }

    if (pcm_stream != BTUSB_LITE_AV_PCM_CHANNEL)
    {
        BTUSB_ERR("Bad stream=%d\n", pcm_stream);
        return;
    }

    /* Get Reference on the SBC Stream (which is the same than the Encoder channel) */
    p_av_cb = &((struct btusb_data *)p_dev)->lite_cb.av;

    if (p_av_cb->pcm.state != BTPCM_LITE_PCM_STARTED)
    {
        BTUSB_ERR("BTPCM is not started\n");
        btpcm_stop(pcm_stream);
        return;
    }

    /* Get reference to AV's encoder */
    p_encoder = &p_av_cb->encoder;

    /* Calculate the size (in byte) of an Input PCM buffer (holding one encoded frame) */
    pcm_frame_size_byte = p_av_cb->encoder.pcm_frame_size;
    pcm_frame_size_byte *= 2; /* Stereo */
    pcm_frame_size_byte *= 2; /* 16 bits per sample */

    /* Sanity Check */
    if (pcm_frame_size_byte == 0)
    {
        BTUSB_ERR("Bad PCM Frame size=%d\n", pcm_frame_size_byte);
        return;
    }

#if 0
    if (num1 == 0) {
	    netlink_sock_init();
	    num1++;
    } else if (pid > 0) {
	    preempt_disable();
	    ptr = p_data;
	    num = nb_pcm_frames;
	    printk("################### Dump PCM data to file ###################\n");
	    /* Fill the ACL Packet with SBC Frames */
	    do
	    {
		    printk("pcm_frame_size_byte = %d\n", pcm_frame_size_byte);
		    send_data_to_userspace(ptr, pcm_frame_size_byte);
#if 0
		    for (num1 = 0; num1 < pcm_frame_size_byte; num1++) {
			    printk("%2.2x ", ((unsigned char *)ptr)[num1]);
			    if(0 == ((num1 + 1) % 20))
				    printk("\n");
		    }
		    printk("\n\n");
#endif
		    ptr += pcm_frame_size_byte; /* Jump to the next PCM sample */
		    num -= pcm_frame_size_byte / 4; /* Update number of remaining samples */

	    } while (num > 0);
	    printk("################### Dump PCM data to file ###################\n");
    }
#endif

    /*
     * No PCM data in a timer period.
     * Need to send AV data in a working buffer if it exists
     */
    if (nb_pcm_frames == 0)
    {
        if (p_av_cb->p_buf_working != NULL)
        {
            if (p_av_cb->p_buf_working->len)
            {
                /*  For AV channel, send the packet */
                btusb_av_send_packet((struct btusb_data *)p_dev, p_av_cb->p_buf_working, p_av_cb->p_buf_working->len);

                /* A new working buffer must be allocated */
                p_av_cb->p_buf_working = NULL;
            }
        }
        return;
    }

    /* While received buffer is not empty */
    while (nb_pcm_frames)
    {
	    /* Check if there are enough remaining frames in the buffer */
	    if ((nb_pcm_frames * 2 * 2) < pcm_frame_size_byte)
	    {
		    BTUSB_ERR("Bad nb_pcm_frames=%d\n", nb_pcm_frames);
		    return;
	    }

	    /* If no working buffer allocated */
	    if (p_av_cb->p_buf_working == NULL)
	    {
		    /* Get a buffer from the pool */
		    p_av_cb->p_buf_working = (BT_HDR *)GKI_getbuf(sizeof(BT_HDR) + p_av_cb->header_len + p_av_cb->payload_len);
		    if(unlikely(p_av_cb->p_buf_working == NULL))
		    {
			    BTUSB_ERR("Unable to get GKI buffer - sent fail\n");
			    return;
		    }

		    if (unlikely(GKI_buffer_status(p_av_cb->p_buf_working) != BUF_STATUS_UNLINKED))
		    {
			    BTUSB_ERR("buffer != BUF_STATUS_UNLINKED 0x%p\n", p_av_cb->p_buf_working);
			    return;
		    }

		    /* Skip headers */
		    p_av_cb->p_buf_working->offset = p_av_cb->header_len;
		    p_av_cb->p_buf_working->len = 0;
		    p_av_cb->p_buf_working->layer_specific = 0; /* Used to store the number of Encoded Frames */
	    }

	    /* Fill the ACL Packet with SBC Frames */
	    do
	    {
		    /* Get Write address */
		    p_dest = (UINT8 *)(p_av_cb->p_buf_working + 1) + p_av_cb->p_buf_working->offset +
			    p_av_cb->p_buf_working->len;
		    if (p_encoder->type == A2D_MEDIA_CT_SBC)
		    {
			    /* Encode one PCM frame with SBC Encoder*/
			    btsbc_encode(p_encoder->channel,
					    /* If Mute => Zero filled PCM sample*/
					    /* Otherwise => regular PCM data */
					    //pcm0_mute?btusb_lite_silence_pcm:p_data,
					    p_data,
					    pcm_frame_size_byte,
					    p_dest,                 /* SBC Output buffer */
					    p_av_cb->encoder.encoded_frame_size, /* Expected Output SBC frame size */
					    &written_enc_size);
		    }
#ifdef BTUSB_LITE_SEC
		    else if (p_encoder->type == A2D_MEDIA_CT_SEC)
		    {
			    /* Encode one PCM frame with SEC Encoder*/
			    written_enc_size = btsec_encode(p_av_cb->encoder.channel,
					    /* If Mute => Zero filled PCM sample*/
					    /* Otherwise => regular PCM data */
					    //pcm0_mute?btusb_lite_silence_pcm:p_data,
					    p_data,
					    pcm_frame_size_byte,
					    p_dest,                 /* SEC Output buffer */
					    p_encoder->encoded_frame_size);/* Expected Output SEC frame size */
		    }
#endif
		    else
		    {
			    BTUSB_ERR("Bad Encoding TYPE:%d)\n", p_encoder->type);
			    written_enc_size = 0;
		    }

		    if (written_enc_size != p_av_cb->encoder.encoded_frame_size)
		    {
			    BTUSB_ERR("Bad Encoded Fame lenght=%d (expected=%d)\n",
					    written_enc_size, p_av_cb->encoder.encoded_frame_size);
		    }

		    /* Update Encoded packet length */
		    p_av_cb->p_buf_working->len += (UINT16)p_av_cb->encoder.encoded_frame_size;

		    /* One more Encoded Frame */
		    p_av_cb->p_buf_working->layer_specific++;

		    p_data += pcm_frame_size_byte; /* Jump to the next PCM sample */
		    nb_pcm_frames -= pcm_frame_size_byte / 4; /* Update number of remaining samples */

	    } while (nb_pcm_frames &&
			    (p_av_cb->p_buf_working->layer_specific < A2D_SBC_HDR_NUM_MSK) &&
			    ((p_av_cb->p_buf_working->len + p_av_cb->encoder.encoded_frame_size) < p_av_cb->curr_mtu));
	    /* If no more room to store an encoded frame */
	    if (p_av_cb->encoder.encoded_frame_size > (p_av_cb->curr_mtu - p_av_cb->p_buf_working->len))
	    {
		    /*  For AV channel, send the packet */
		    btusb_av_send_packet((struct btusb_data *)p_dev, p_av_cb->p_buf_working, p_av_cb->p_buf_working->len);

		    /* A new working buffer must be allocated */
		    p_av_cb->p_buf_working = NULL;
	    }
    }
}

static int btusb_av_sbc_start(struct btusb_data *data, UINT8 scb_idx,
        tBTA_AV_AUDIO_CODEC_INFO*p_codec_cfg)
{
    struct btusb_lite_av_cb *p_av_cb = &data->lite_cb.av;
    struct btusb_lite_encoder_ccb *p_encoder;
    struct btusb_lite_av_scb *p_av_scb;
    int nb_sbc_frames;
    int rv;
    int bitpool;
    struct btusb_av_sbc_param sbc_param;
    int av_header_len;

    /* Parse SBC Codec Info */
    if (btusb_av_parse_sbc_codec(&sbc_param, p_codec_cfg->codec_info) < 0)
    {
        BTUSB_ERR("Bad SBC Codec. Stream not started\n");
        return -1;
    }

    if (scb_idx >= BTA_AV_NUM_STRS)
    {
        BTUSB_ERR("Bad scb_idx=%d", scb_idx);
        return -1;
    }
    p_av_scb = &p_av_cb->scb[scb_idx];

    /* Calculate the BitPool for this BitRate */
    bitpool = btusb_sbc_get_bitpool(&sbc_param, p_codec_cfg->bit_rate);
    if (bitpool <= 0)
    {
        BTUSB_ERR("btusb_lite_sbc_get_bitpool return wrong bitpool=%d\n", bitpool);
        return -1;
    }
    BTUSB_INFO("SBC BitPool=%d\n", bitpool);

    p_av_cb->timestamp = 0; /* Reset TimeStamp */
    p_av_cb->option = 0; /* No specific Option (RTP and Media Payload Header presents) */
    p_av_cb->m_pt = AVDT_RTP_PAYLOAD_TYPE | A2D_MEDIA_CT_SBC;
    p_av_cb->m_pt &= ~AVDT_MARKER_SET;

    /* Calculate Packet Header Size (HCI, L2CAP, RTP and MediaPayloadHeader) */
    p_av_cb->header_len = BTUSB_LITE_HCI_ACL_HDR_SIZE +
                          BTUSB_LITE_L2CAP_HDR_SIZE +
                          BTUSB_LITE_RTP_SIZE +
                          BTUSB_LITE_SCMS_SIZE +
                          BTUSB_LITE_MEDIA_SIZE;
    /* Calculate AV Header Size */
    av_header_len = BTUSB_LITE_L2CAP_HDR_SIZE +
                    BTUSB_LITE_RTP_SIZE +
                    BTUSB_LITE_SCMS_SIZE +
                    BTUSB_LITE_MEDIA_SIZE;

    /* clear the congestion flag: full stack made it congested when opening */
    p_av_scb->cong = FALSE;
    p_av_scb->started = TRUE;

    /* Get reference to AV's encoder */
    p_encoder = &p_av_cb->encoder;

    if (p_encoder->opened == 0)
    {
        /* Allocate an SBC Channel */
        rv = btsbc_alloc();
        if (rv < 0)
        {
            BTUSB_ERR("btsbc_alloc failed\n");
            return -1;
        }
        p_encoder->opened = 1;
        p_encoder->channel = rv;
        p_encoder->type = A2D_MEDIA_CT_SBC;
    }

    /* Configure the SBC Channel */
    rv = btsbc_config(p_encoder->channel,
            sbc_param.frequency,
            sbc_param.nb_blocks,
            sbc_param.nb_subbands,
            sbc_param.mode,
            sbc_param.allocation,
            (unsigned char)bitpool);
    if (rv <= 0)
    {
        BTUSB_ERR("btsbc_config failed\n");
        btsbc_free(p_encoder->channel);
        p_encoder->opened = 0;
        return -1;
    }

    /* Save the calculated SBC Frame size */
    p_encoder->encoded_frame_size = rv;
    BTUSB_INFO("encoded_frame_size=%d\n", rv);

    /* Configure the PCM Channel */
    rv = btpcm_config(BTUSB_LITE_AV_PCM_CHANNEL,
            data,
            sbc_param.frequency,
            sbc_param.mode==CODEC_MODE_MONO?1:2,
            16, /* SBC Encoder requires 16 bits per sample */
            btusb_av_pcm_cback);
    if (rv < 0)
    {
        BTUSB_ERR("btpcm_config failed\n");
        return -1;
    }


    /* Calculate and save the PCM frame size */
    p_encoder->pcm_frame_size = sbc_param.nb_blocks * sbc_param.nb_subbands;
    BTUSB_INFO("pcm_frame_size=%d\n", p_encoder->pcm_frame_size);

#if 0
    /* Calculate nb_sbc_frames depending on MTU */
    nb_sbc_frames = (p_av_cb->curr_mtu - av_header_len) / p_encoder->encoded_frame_size;
#else
    nb_sbc_frames = 10;
    if(p_av_cb->stack_mtu < (p_encoder->encoded_frame_size * nb_sbc_frames + av_header_len))
    {   /* if 10 SBC frame is bigger than mtu size, should change nb_sbc_frames value to fit in mtu */
        nb_sbc_frames = (p_av_cb->stack_mtu - av_header_len) / p_encoder->encoded_frame_size;
        p_av_cb->curr_mtu = p_av_cb->stack_mtu;
    }
    else
    {
        p_av_cb->curr_mtu = p_encoder->encoded_frame_size * nb_sbc_frames + av_header_len;
    }
#endif
    BTUSB_INFO("mtu:%d, nb_sbc_frames:%d, encoded_frame_size%d\n",
              p_av_cb->curr_mtu, nb_sbc_frames, p_encoder->pcm_frame_size);

    /* Calculate the size of the Payload */
    p_av_cb->payload_len = nb_sbc_frames * p_encoder->encoded_frame_size;

    BTUSB_INFO("nb_sbc_frames=%d payload_len=%d\n", nb_sbc_frames, p_av_cb->payload_len);

    GKI_init();

    /* Start the PCM stream */
    rv = btpcm_start(BTUSB_LITE_AV_PCM_CHANNEL,
            p_encoder->pcm_frame_size, nb_sbc_frames, 0);
    if (rv < 0)
    {
        BTUSB_ERR("btpcm_start failed\n");
        return -1;
    }
    p_av_cb->pcm.state = BTPCM_LITE_PCM_STARTED;
    return 0;
}

/* Add PCM read functionality from BRCM driver */
/*******************************************************************************
**
** Function         btusb_lite_av_add
**
** Description      Add (Sync) an AV channel.
**
** Returns          None.
**
*******************************************************************************/

#if 0
typedef struct
{
    UINT8               avdt_handle;    /* AVDTP handle */
    UINT8               chnl;           /* the channel: audio/video */
    UINT8               codec_type;     /* codec type */
    BOOLEAN             cong;           /* TRUE if AVDTP congested */
    UINT8               hdi;            /* the index to SCB[] */
    UINT8               hndl;           /* the handle: ((hdi + 1)|chnl) */
    UINT8               l2c_bufs;       /* the number of buffers queued to L2CAP */
    UINT16              l2c_cid;        /* L2CAP channel ID */
    BD_ADDR             peer_addr;      /* peer BD address */
}tBTA_AV_SYNC_INFO;

multi_av_supported: always 0
curr_mtu: MTU size
#endif

void btusb_av_add(struct btusb_data *data, tBTA_AV_SYNC_INFO *p_sync_info,
        UINT8 multi_av_supported, UINT16 curr_mtu)
{
    struct btusb_lite_av_cb *p_av_cb = &data->lite_cb.av;
    struct btusb_lite_av_scb *p_av_scb;
    int rv;

    /* Hardcoded!! */
    p_sync_info->avdt_handle = 1;
    p_sync_info->chnl = 0; // Need to check - use or not
    p_sync_info->codec_type = A2D_MEDIA_CT_SBC;
    p_sync_info->cong = TRUE; // Need to check - use or not
    p_sync_info->hdi = 0;
    p_sync_info->hndl = 1; // Need to check this value!!!!!
    p_sync_info->l2c_bufs = 0;
    p_sync_info->l2c_cid = 0;

    //As peer_addr is not used, just hardcode it to dummy value */
    p_sync_info->peer_addr[0] = 0x00;
    p_sync_info->peer_addr[1] = 0x01;
    p_sync_info->peer_addr[2] = 0x02;
    p_sync_info->peer_addr[3] = 0x03;
    p_sync_info->peer_addr[4] = 0x04;
    p_sync_info->peer_addr[5] = 0x05;
    /* End hardcoded */


    p_av_cb->stack_mtu = curr_mtu;   /* Update MTU */
    p_av_cb->curr_mtu = curr_mtu;   /* Update MTU */

    if (p_sync_info->hdi >= BTA_AV_NUM_STRS)
    {
        BTUSB_ERR("Bad AV Index=%d\n", p_sync_info->hdi);
        return;
    }

#if (BTU_MULTI_AV_INCLUDED == TRUE)
    p_av_cb->multi_av &= ~(BTA_AV_MULTI_AV_SUPPORTED);
    p_av_cb->multi_av |= multi_av_supported;
#endif

    p_av_scb = &p_av_cb->scb[p_sync_info->hdi];

    p_av_scb->avdt_handle = p_sync_info->avdt_handle;
    p_av_scb->chnl = p_sync_info->chnl;
    p_av_scb->codec_type = p_sync_info->codec_type;
    p_av_scb->cong = p_sync_info->cong;
    p_av_scb->hdi = p_sync_info->hdi;
    p_av_scb->hndl = p_sync_info->hndl;
    p_av_scb->l2c_bufs = p_sync_info->l2c_bufs;
    p_av_scb->l2c_cid = p_sync_info->l2c_cid;
    memcpy(p_av_scb->peer_addr, p_sync_info->peer_addr, BD_ADDR_LEN);

    if (p_av_cb->pcm.state == BTPCM_LITE_PCM_CLOSED)
    {
        /* Open the PCM Channel */
        rv = btpcm_open(BTUSB_LITE_AV_PCM_CHANNEL);
        if (rv < 0)
        {
            BTUSB_ERR("btpcm_open failed\n");
            return;
        }
        p_av_cb->pcm.state = BTPCM_LITE_PCM_OPENED;
        p_av_cb->pcm.frequency = -1;
        p_av_cb->pcm.channel = BTUSB_LITE_AV_PCM_CHANNEL;
    }
	btusb_avdt_init_scb(data);
}

/*******************************************************************************
**
** Function         btusb_lite_av_remove
**
** Description      Remove (Cleanup) an AV channel.
**
** Returns          None.
**
*******************************************************************************/
void btusb_av_remove(struct btusb_data *data, UINT8 scb_idx,
        UINT8 audio_open_cnt, UINT16 curr_mtu)
{
    struct btusb_lite_av_cb *p_av_cb = &data->lite_cb.av;
    struct btusb_lite_av_scb *p_av_scb;
    int av_scb;
    int cleanup_needed = 1;
    int rv;

/* Hardcoded!! */
    curr_mtu = REAL_CONNECTED_MTU;
    scb_idx = 0;
    audio_open_cnt = 1;
/* End hardcoded */


    p_av_cb->curr_mtu = curr_mtu;   /* Update MTU */
    p_av_cb->audio_open_cnt = audio_open_cnt;   /* Update audio_open_cnt */

    if (scb_idx >= BTA_AV_NUM_STRS)
    {
        BTUSB_ERR("Bad Index=%d\n", scb_idx);
        return;
    }

    p_av_scb = &p_av_cb->scb[scb_idx];

    /* Remove AVDT CCB and SCB */
    btusb_avdt_remove_scb(data,  p_av_scb->avdt_handle, NULL);

    /* Clear the AV Stream Control Clock */
    memset(p_av_scb, 0, sizeof(*p_av_scb));
//#if 0
    /* Check this is the last AV channel removed */
    p_av_scb = &p_av_cb->scb[0];
    for (av_scb = 0 ; av_scb < BTA_AV_NUM_STRS ; av_scb++, p_av_scb++)
    {
        if (p_av_scb->hndl)
        {
            cleanup_needed = 1;
            break;
        }
    }
//#endif
    if (cleanup_needed)
    {
        if (p_av_cb->pcm.state == BTPCM_LITE_PCM_STARTED)
        {
            /* Stop the PCM Channel */
            rv = btpcm_stop(BTUSB_LITE_AV_PCM_CHANNEL);
            if (rv < 0)
            {
                BTUSB_ERR("btpcm_close failed\n");
            }
            p_av_cb->pcm.state = BTPCM_LITE_PCM_CONFIGURED;
        }

        if (p_av_cb->pcm.state != BTPCM_LITE_PCM_CLOSED)
        {
            /* Close the PCM Channel */
            rv = btpcm_close(BTUSB_LITE_AV_PCM_CHANNEL);
            if (rv < 0)
            {
                BTUSB_ERR("btpcm_close failed\n");
            }
            p_av_cb->pcm.state = BTPCM_LITE_PCM_CLOSED;
        }

        if (p_av_cb->encoder.opened)
        {
            switch(p_av_cb->encoder.type)
            {
            case A2D_MEDIA_CT_SBC:
                btsbc_free(p_av_cb->encoder.channel);
                break;
            default:
                BTUSB_ERR("Unknown Encoder type=%d\n", p_av_cb->encoder.encoder.codec_type);
                break;
            }
            p_av_cb->encoder.opened = 0;
        }
    }
}




/*******************************************************************************
**
** Function         btusb_lite_av_start
**
** Description      Start AV
**
** Returns          None.
**
*******************************************************************************/

/*
scb_idx: Always 0


*/

#if 0
typedef struct
{
    UINT16  bit_rate;       /* SBC encoder bit rate in kbps */
    UINT16  bit_rate_busy;  /* SBC encoder bit rate in kbps */
    UINT16  bit_rate_swampd;/* SBC encoder bit rate in kbps */
    UINT8   busy_level;     /* Busy level indicating the bit-rate to be used */
    UINT8   codec_info[AVDT_CODEC_SIZE];
    UINT8   codec_type;     /* Codec type */
} tBTA_AV_AUDIO_CODEC_INFO;
#endif

void btusb_av_start(struct btusb_data *data, UINT8 scb_idx, UINT8 start_stop_flag,
        UINT8 audio_open_cnt, tBTA_AV_AUDIO_CODEC_INFO *p_codec_cfg)
{
    struct btusb_lite_av_cb *p_av_cb = &data->lite_cb.av;
    struct btusb_lite_av_scb *p_av_scb;

    /* Hardcoded values, later we use values from stack */
    scb_idx = 0;
    start_stop_flag = 0; // suspand
    audio_open_cnt = 1;
    p_codec_cfg->bit_rate = 250;
    p_codec_cfg->bit_rate_busy = 200;
    p_codec_cfg->bit_rate_swampd = 0;
    p_codec_cfg->busy_level = 0;
    p_codec_cfg->codec_type = A2D_MEDIA_CT_SBC;

    p_codec_cfg->codec_info[0] = CODEC_SBC_LOSC;
    p_codec_cfg->codec_info[1] = 0x00;  /* Ignore MT */

    p_codec_cfg->codec_info[2] = 0x00; /* Codec Type */
    p_codec_cfg->codec_info[3] = CODEC_SBC_FREQ_48 | CODEC_MODE_JOIN_STEREO;
    p_codec_cfg->codec_info[4] = CODEC_SBC_BLOCK_16 | CODEC_SBC_NBBAND_8 | CODEC_SBC_ALLOC_LOUDNESS;
    p_codec_cfg->codec_info[5] = 0x23; // bitpool_min
    p_codec_cfg->codec_info[6] = 0x35; // bitpool_max
    p_codec_cfg->codec_info[7] = 0x00;
    p_codec_cfg->codec_info[8] = 0x00;
    p_codec_cfg->codec_info[9] = 0x00;
    /* End Hardcoded */


    if (scb_idx >= BTA_AV_NUM_STRS)
    {
        BTUSB_ERR("Bad scb_idx=%d", scb_idx);
        return;
    }

    p_av_scb = &p_av_cb->scb[scb_idx];

    if (start_stop_flag)
    {
        p_av_cb->scb[scb_idx].started = FALSE;
        BTUSB_ERR("start_stop_flag TODO!!!");
    }
    else
    {
        /* If the Codec Type is SBC */
        if (p_codec_cfg->codec_type == A2D_MEDIA_CT_SBC)
        {
            if (btusb_av_sbc_start(data, scb_idx, p_codec_cfg) < 0)
            {
                BTUSB_ERR("SBC Stream not started\n");
                return;
            }
        }
        else
        {
            BTUSB_ERR("Unsupported Encoder type=%d\n", p_codec_cfg->codec_type);
        }
    }
}

static void btusb_av_config(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	tBTA_AV_SYNC_INFO p_sync_info;
	UINT16 av_peer_mtu = 0;
	/* We support one AV streaming now, later we use value from stack*/
	UINT8 multi_av_supported = 0;
	/* Get peer MTU value from L2CAP */
	av_peer_mtu = l2cap_audio_get_peer_mtu_size();
	BTUSB_INFO("Peer MTU size = %d", av_peer_mtu);
	UINT16 curr_mtu = av_peer_mtu - BTUSB_LITE_RTP_SIZE; //Set current MTU value

	btusb_av_add(data, &p_sync_info, multi_av_supported, curr_mtu);

	return;
}

static void btusb_av_open(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	UINT8 scb_idx = 0;
	UINT8 start_stop_flag = 0;
	UINT8 audio_open_cnt = 1;
	tBTA_AV_AUDIO_CODEC_INFO p_codec_cfg;

	btusb_av_start(data, scb_idx, start_stop_flag, audio_open_cnt, &p_codec_cfg);

	return;
}

static void btusb_av_close(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	UINT8 scb_idx = 0;
	UINT8 audio_open_cnt = 1;
	UINT16 curr_mtu = AV_MAX_A2DP_MTU;
	btusb_av_remove(data, scb_idx, audio_open_cnt, curr_mtu);
	return;
}


#endif

static int inc_tx(struct btusb_data *data)
{
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&data->txlock, flags);
	rv = test_bit(BTUSB_SUSPENDING, &data->flags);
	if (!rv)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);

	return rv;
}

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
						btusb_intr_complete, hdev,
						data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btusb_submit_bulk_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s", hdev->name);

	if (!data->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
					buf, size, btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_isoc_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hci_get_drvdata(hdev);
	int i, err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (hci_recv_fragment(hdev, HCI_SCODATA_PKT,
						urb->transfer_buffer + offset,
								length) < 0) {
				BT_ERR("%s corrupted SCO packet", hdev->name);
				hdev->stat.err_rx++;
			}
		}
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btusb_submit_isoc_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	if (!data->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size, btusb_isoc_complete,
				hdev, data->isoc_rx_ep->bInterval);

	urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	spin_lock(&data->txlock);
	data->tx_in_flight--;
	spin_unlock(&data->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btusb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return err;

	data->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		goto done;

	err = btusb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btusb_submit_bulk_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&data->intr_anchor);
		goto failed;
	}

	set_bit(BTUSB_BULK_RUNNING, &data->flags);
	btusb_submit_bulk_urb(hdev, GFP_KERNEL);

done:
	usb_autopm_put_interface(data->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	clear_bit(HCI_RUNNING, &hdev->flags);
	usb_autopm_put_interface(data->intf);
	return err;
}

static void btusb_stop_traffic(struct btusb_data *data)
{
	usb_kill_anchored_urbs(&data->intr_anchor);
	usb_kill_anchored_urbs(&data->bulk_anchor);
	usb_kill_anchored_urbs(&data->isoc_anchor);
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	cancel_work_sync(&data->work);
	cancel_work_sync(&data->waker);

	clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);

	btusb_stop_traffic(data);
	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto failed;

	data->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(data->intf);

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
	return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static int btusb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = data->cmdreq_type;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(data->udev, 0x00);

		usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		if (!data->bulk_tx_ep)
			return -ENODEV;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndbulkpipe(data->udev,
					data->bulk_tx_ep->bEndpointAddress);

		usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		if (!data->isoc_tx_ep || hdev->conn_hash.sco_num < 1)
			return -ENODEV;

		urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndisocpipe(data->udev,
					data->isoc_tx_ep->bEndpointAddress);

		usb_fill_int_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_isoc_tx_complete,
				skb, data->isoc_tx_ep->bInterval);

		urb->transfer_flags  = URB_ISO_ASAP;

		__fill_isoc_descriptor(urb, skb->len,
				le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

		hdev->stat.sco_tx++;
		goto skip_waking;

	default:
		return -EILSEQ;
	}

	err = inc_tx(data);
	if (err) {
		usb_anchor_urb(urb, &data->deferred);
		schedule_work(&data->waker);
		err = 0;
		goto done;
	}

skip_waking:
	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}

done:
	usb_free_urb(urb);
	return err;
}

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);

	if (hdev->conn_hash.sco_num != data->sco_num) {
		data->sco_num = hdev->conn_hash.sco_num;
		schedule_work(&data->work);
	}
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!data->isoc)
		return -ENODEV;

	err = usb_set_interface(data->udev, 1, altsetting);
	if (err < 0) {
		BT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}

	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			data->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
		BT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;
	int new_alts;
	int err;

	if (hdev->conn_hash.sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
			err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}

		if (hdev->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };
			new_alts = alts[hdev->conn_hash.sco_num - 1];
		} else {
			new_alts = hdev->conn_hash.sco_num;
		}

		if (data->isoc_altsetting != new_alts) {
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			usb_kill_anchored_urbs(&data->isoc_anchor);

			if (__set_isoc_interface(hdev, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
			if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			else
				btusb_submit_isoc_urb(hdev, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);

		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
	}
}

static void btusb_waker(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, waker);
	int err;

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return;

	usb_autopm_put_interface(data->intf);
}

static int btusb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btusb_data *data;
	struct hci_dev *hdev;
	int i, err;

	BT_INFO("[BTUSB]: Start Probe");
	BT_DBG("intf %p id %p", intf, id);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	if (!id->driver_info) {
		const struct usb_device_id *match;
		match = usb_match_id(intf, blacklist_table);
		if (match)
			id = match;
	}

	if (id->driver_info == BTUSB_IGNORE)
		return -ENODEV;

	if (ignore_dga && id->driver_info & BTUSB_DIGIANSWER)
		return -ENODEV;

	if (ignore_csr && id->driver_info & BTUSB_CSR)
		return -ENODEV;

	if (ignore_sniffer && id->driver_info & BTUSB_SNIFFER)
		return -ENODEV;

	if (id->driver_info & BTUSB_ATH3012) {
		struct usb_device *udev = interface_to_usbdev(intf);

		/* Old firmware would otherwise let ath3k driver load
		 * patch and sysconfig files */
		if (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x0001)
			return -ENODEV;
	}

	data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep)
		return -ENODEV;

	data->cmdreq_type = USB_TYPE_CLASS;

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;

	spin_lock_init(&data->lock);

	INIT_WORK(&data->work, btusb_work);
	INIT_WORK(&data->waker, btusb_waker);
	spin_lock_init(&data->txlock);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->deferred);

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_USB;
	hci_set_drvdata(hdev, data);

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = btusb_open;
	hdev->close    = btusb_close;
	hdev->flush    = btusb_flush;
	hdev->send     = btusb_send_frame;
	hdev->notify   = btusb_notify;

	hdev->av_config   = btusb_av_config;
	hdev->av_start   = btusb_av_open;
	hdev->av_stop   = btusb_av_close;

	/* Interface numbers are hardcoded in the specification */
	data->isoc = usb_ifnum_to_if(data->udev, 1);

	if (!reset)
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	if (force_scofix || id->driver_info & BTUSB_WRONG_SCO_MTU) {
		if (!disable_scofix)
			set_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_BROKEN_ISOC)
		data->isoc = NULL;

	if (id->driver_info & BTUSB_DIGIANSWER) {
		data->cmdreq_type = USB_TYPE_VENDOR;
		set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_CSR) {
		struct usb_device *udev = data->udev;

		/* Old firmware would otherwise execute USB reset */
		if (le16_to_cpu(udev->descriptor.bcdDevice) < 0x117)
			set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_SNIFFER) {
		struct usb_device *udev = data->udev;

		/* New sniffer firmware has crippled HCI interface */
		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);

		data->isoc = NULL;
	}
#if 0
	if (id->driver_info & BTUSB_BCM92035) {
		unsigned char cmd[] = { 0x3b, 0xfc, 0x01, 0x00 };
		struct sk_buff *skb;

		skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
		if (skb) {
			memcpy(skb_put(skb, sizeof(cmd)), cmd, sizeof(cmd));
			skb_queue_tail(&hdev->driver_init, skb);
		}
	}
#endif
	if (data->isoc) {
		err = usb_driver_claim_interface(&btusb_driver,
							data->isoc, data);
		if (err < 0) {
			hci_free_dev(hdev);
			return err;
		}
	}

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		return err;
	}

	usb_set_intfdata(intf, data);

	BT_INFO("[BTUSB]: Finished Probe");
	return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_INFO("[BTUSB]: Remove");
	BT_DBG("intf %p", intf);

	if (!data)
		return;

	hdev = data->hdev;
	usb_set_intfdata(data->intf, NULL);

	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);

	hci_unregister_dev(hdev);

	if (intf == data->isoc)
		usb_driver_release_interface(&btusb_driver, data->intf);
	else if (data->isoc)
		usb_driver_release_interface(&btusb_driver, data->isoc);

	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btusb_data *data = usb_get_intfdata(intf);

	BT_INFO("[BTUSB]: Suspend");
	BT_DBG("intf %p", intf);

	if (data->suspend_count++)
		return 0;

	spin_lock_irq(&data->txlock);
	if (!(PMSG_IS_AUTO(message) && data->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &data->flags);
		spin_unlock_irq(&data->txlock);
	} else {
		spin_unlock_irq(&data->txlock);
		data->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&data->work);

	btusb_stop_traffic(data);
	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static void play_deferred(struct btusb_data *data)
{
	struct urb *urb;
	int err;

	while ((urb = usb_get_from_anchor(&data->deferred))) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0)
			break;

		data->tx_in_flight++;
	}
	usb_scuttle_anchored_urbs(&data->deferred);
}

static int btusb_resume(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;
	int err = 0;

	BT_INFO("[BTUSB]: Resume");
	BT_DBG("intf %p", intf);

	if (--data->suspend_count)
		return 0;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
		err = btusb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &data->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		err = btusb_submit_bulk_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
			goto failed;
		}

		btusb_submit_bulk_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btusb_submit_isoc_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btusb_submit_isoc_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&data->txlock);
	play_deferred(data);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);
	schedule_work(&data->work);

	return 0;

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
done:
	spin_lock_irq(&data->txlock);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);

	return err;
}
#endif

static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
#ifdef CONFIG_PM
	.suspend	= btusb_suspend,
	.resume		= btusb_resume,
#endif
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(btusb_driver);

module_param(ignore_dga, bool, 0644);
MODULE_PARM_DESC(ignore_dga, "Ignore devices with id 08fd:0001");

module_param(ignore_csr, bool, 0644);
MODULE_PARM_DESC(ignore_csr, "Ignore devices with id 0a12:0001");

module_param(ignore_sniffer, bool, 0644);
MODULE_PARM_DESC(ignore_sniffer, "Ignore devices with id 0a12:0002");

module_param(disable_scofix, bool, 0644);
MODULE_PARM_DESC(disable_scofix, "Disable fixup of wrong SCO buffer size");

module_param(force_scofix, bool, 0644);
MODULE_PARM_DESC(force_scofix, "Force fixup of wrong SCO buffers size");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Generic Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
