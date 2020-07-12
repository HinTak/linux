/***************************************************************************
 *
 *	drivers/net/ethernet/sdp/sdp_mac.h
 *	Samsung Elecotronics.Co
 *	Created by tukho.kim
 *
 * ************************************************************************/
/*
 * 2009.08.02: Created by tukho.kim@samsung.com
 * 2009.10.22,tukho.kim: debug when rx buffer alloc is failed 0.940
 * 2009.12.01,tukho.kim: debug phy check media, 0.951
 *                       full <-> half ok, 10 -> 100 ok, 100 -> 10 need to unplug and plug cable
 * 2012.04.23,drain.lee: add NAPI struct.
 * 2012.05.14,drain.lee: move chip dependent code. 0.969
 * 2012.07.23,drain.lee: add flow ctrl value 0.973
 * 2012.10.05,drain.lee: change debug print format v0.979
 * 2012.12.21,drain.lee: fix compile warning v0.985
 * 2013.05.07,drain.lee: remove dummy skb v0.999
 */


#ifndef __SDP_GMAC_H

/* define debug */

#ifdef CONFIG_SDP_MAC_DEBUG
#define DPRINTK_GMAC_DBG(fmt,args...)	printk(KERN_DEBUG "["ETHER_NAME" DBG %s] " fmt,__FUNCTION__,##args)
#define DPRINTK_GMAC_FLOW(fmt,args...)	printk(KERN_DEBUG "["ETHER_NAME" FLOW %s] " fmt,__FUNCTION__,##args)
#define DPRINTK_GMAC(fmt,args...)		printk(KERN_DEBUG "["ETHER_NAME" %s] " fmt,__FUNCTION__,##args)
#else
#define DPRINTK_GMAC_DBG(fmt,args...)	if(0) printk(KERN_DEBUG "["ETHER_NAME" DBG %s] " fmt,__FUNCTION__,##args)
#define DPRINTK_GMAC_FLOW(fmt,args...)	if(0) printk(KERN_DEBUG "["ETHER_NAME" FLOW %s] " fmt,__FUNCTION__,##args)
#define DPRINTK_GMAC(fmt,args...)		if(0) printk(KERN_DEBUG "["ETHER_NAME" %s] " fmt,__FUNCTION__,##args)
#endif


#define DPRINTK_GMAC_ERROR(fmt,args...)	printk(KERN_ERR "["ETHER_NAME" ERR %s] " fmt,__FUNCTION__,##args)
#define PRINTK_GMAC(fmt,args...)		printk(KERN_INFO "["ETHER_NAME"] " fmt,##args)

#define SDP_GMAC_OK 	0
#define SDP_GMAC_ERR 	1

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>

/* Ethernet header */
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

/* Io Header */
#include <asm/uaccess.h>
#include <asm/io.h>

#include "sdp_gmac_reg.h"	// 0.952
#include "sdp_mac_phy.h"

//#define ETHER_NAME		"SDP_GMAC" // 0.952 move to sdpGmac_reg.h

#define N_MAC_ADDR	 	ETH_ALEN
#define DEFAULT_MAC_ADDR 	{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}

#if defined(CONFIG_ARCH_SDP1601) || defined(CONFIG_ARCH_SDP1803) || defined(CONFIG_ARCH_SDP_NIKE)

#if defined(CONFIG_ARCH_SDP1803) || defined(CONFIG_ARCH_SDP_NIKE)
#define OC_PACKET_COUNTER_RESET		0x00BE4C00
#define CRC0_PACKET_COUNTER			0x00BE4C04
#define CRC0_CRC_COUNTER			0x00BE4C08
#define CRC1_PACKET_COUNTER			0x00BE4C10
#define CRC1_CRC_COUNTER			0x00BE4C14
#define CRC2_PACKET_COUNTER			0x00BE4C1C
#define CRC2_CRC_COUNTER			0x00BE4C20
#define CRC3_PACKET_COUNTER			0x00BE4C28
#define CRC3_CRC_COUNTER			0x00BE4C2C
#define MAC_PACKET_COUNTER_CRC		0x00BE4C20

extern int sdp_ocl_set_eth_loopback(int enable);
#endif

#define SDP_GMAC_USE_AXI_BUSMASTER
extern int tztv_sys_get_platform_info(void);	// 0 == M2e, 2 == M2, 3 == M2s(OCL)
extern int SDP_OCL_I2C_MaskWriteWord(u32 slaveAddr, u32 wrSubAddr, u32 wrBuf, u32 mask);
extern int SDP_OCL_I2C_ReadWord(u32 slaveAddr, u32 rdSubAddr, u32 *rdBuf);
#endif

struct sdp_gmac_dev;


/* info of current working item in ring */
typedef struct sdp_mac_desc_info {
	unsigned long		index;
	unsigned long		ring_size;
	DMA_DESC_T			*desc;
} sdp_mac_desc_info_t;

typedef bool (*sdp_mac_ring_op_t)(struct sdp_gmac_dev *pGmacDev, sdp_mac_desc_info_t *info, DMA_DESC_T *item_desc);

typedef struct sdp_mac_desc_ring {
	const char			*name;
	unsigned long		head;
	unsigned long		tail;
	unsigned long		size;
	spinlock_t			lock;
	DMA_DESC_T			*pdesc;
	sdp_mac_ring_op_t	push;
	sdp_mac_ring_op_t	pull;
} sdp_mac_desc_ring_t;

typedef struct {
	u32 dma_busMode;
	u32 dma_operationMode;
	u32 gmac_configuration;
	u32 gmac_frameFilter;
	u32 gmac_macAddr[32];
}SDP_GMAC_POWER_T;


/* priv statistics data */
typedef struct {
	u64 rx_unicast_packet;
	u64 rx_broadcast_packet;
	u64 rx_multicast_packet;

	u64 tx_unicast_packet;
	u64 tx_broadcast_packet;
	u64 tx_multicast_packet;
}SDP_GMAC_STATS_T;


typedef struct sdp_gmac_dev {
/* network resouce */
	struct net_device			*pNetDev;
	struct rtnl_link_stats64	stats64;
	SDP_GMAC_STATS_T			priv_stats64;
	struct napi_struct			napi;

/* OS */
	/* mutex resource  */
	spinlock_t					lock;

/* Operation Resource*/
	int							hwDuplex;
	int 						hwSpeed;
	u32							msg_enable;
	u32							bus_align;
	u32							bus_mask;

/* DMA resource - non-cached resource */
	struct device				*pDev;
	struct sdp_mac_desc_ring	rx_ring;
	struct sdp_mac_desc_ring	tx_ring;
	dma_addr_t					txDescDma;	// physical address
	dma_addr_t					rxDescDma;
	DMA_DESC_T					*pTxDesc;	// logical address
	DMA_DESC_T					*pRxDesc;

/* Power Management resource */
	SDP_GMAC_POWER_T			power;

/* register resource */
	SDP_GMAC_T					*pGmacBase;
	SDP_GMAC_MMC_T				*pMmcBase;
	SDP_GMAC_TIME_STAMP_T    	*pTimeStampBase;
	SDP_GMAC_MAC_2ND_BLOCK_T 	*pMac2ndBlk;
	SDP_GMAC_DMA_T				*pDmaBase;

#ifdef CONFIG_ARCH_SDP1004
	int							revision;
#endif

	struct mii_bus				*mdiobus;
	struct phy_device			*phydev;
	int							has_gmac;
	int							oldlink;
	int							is_rx_stop;
	int							forced_phylb;

	enum sdp_mac_flow_ctrl {
		SDP_MAC_FLOW_OFF = 0,
		SDP_MAC_FLOW_RX,
		SDP_MAC_FLOW_TX,
		SDP_MAC_FLOW_RXTX,
	} flow_ctrl;

	int							pause_time;

	unsigned int				polling_interval_us;
	struct hrtimer		polling_timer;

	struct dentry			*debugfs_root;

	struct sdp_gmac_plat		*plat;

}SDP_GMAC_DEV_T;

unsigned long prev_intr_time;
unsigned long intr_timeout_ms = 500; //ms

#if defined(CONFIG_ARCH_SDP_NIKE)
static void *emac_base_addr;
static void *serdes_rx_vdip_addr;
static void *serdes_eth_base_addr;

#define EMAC_CTRL_0 				0x00140000

#define DATA_PATH_SEL_BIT			0	//0x140004[1:0]
#define MDIO_PATH_SEL_BIT			2	//0x140004[3:2]
#define SERDES_LOCKED_BIT			5	//0x140004[5]
#define SERDES_RGMII_BIT			6	//0x140004[6]
#define TX_PAD_CTRL_DELAY_SEL_BIT	0	//0x140008[3:0]
#define TX_PAD_CTRL_PHASE_SEL_BIT	4	//0x140008[7:4]
#define TX_PAD_CTRL_SPEED_SEL_BIT	16	//0x140008[17:16]
#define TX_PAD_CTRL_INV_SEL_BIT		20	//0x140008[20]


#define SERDES_RX_VDIP 				0x00BE0248
#define SERDES_ETH_BASE				0x00BE4000
//#define TX_CLK_CONFIG				0x00BE4098
//#define RX_CLK_CONFIG				0x00BE40A0
#define EMAC_PLL_CONFIG				0x00F50304
#define EMAC_DATA_CLK_CONFIG 		0x00140014
#define EMAC_RMII_CLK_CONFIG 		0x00140018

#endif

#endif /*  __SDP_GMAC_H */

