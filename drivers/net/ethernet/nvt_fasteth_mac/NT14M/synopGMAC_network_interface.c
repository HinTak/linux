/** \file
 * This is the network dependent layer to handle network related functionality.
 * This file is tightly coupled to neworking frame work of linux 2.6.xx kernel.
 * The functionality carried out in this file should be treated as an example only
 * if the underlying operating system is not Linux. 
 * 
 * \note Many of the functions other than the device specific functions
 *  changes for operating system other than Linux 2.6.xx
 * \internal 
 *-----------------------------REVISION HISTORY-----------------------------------
 * Synopsys			01/Aug/2007				Created
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/io.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/sockios.h>		// for socket ioctl lists
#include <linux/mii.h>

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>		// USE_DEVICE_TREE
#include <linux/platform_device.h>

#include "synopGMAC_Host.h"
#include "synopGMAC_plat.h"
#include "synopGMAC_network_interface.h"
#include "synopGMAC_Dev.h"
#include "m_internal_Phy.h"

#define DEBUG_READ_PHY_REG      1

#define NVT_PHY_RESET_GPIO_ADDR 0xfd100400
#define NVT_PHY_RESET_GPIO_BIT	21

#define IOCTL_READ_REGISTER  SIOCDEVPRIVATE+1
#define IOCTL_WRITE_REGISTER SIOCDEVPRIVATE+2
#define IOCTL_READ_IPSTRUCT  SIOCDEVPRIVATE+3
#define IOCTL_READ_RXDESC    SIOCDEVPRIVATE+4
#define IOCTL_READ_TXDESC    SIOCDEVPRIVATE+5
#define IOCTL_POWER_DOWN     SIOCDEVPRIVATE+6
#define IOCTL_CHK_CONN	     SIOCDEVPRIVATE+7
#define IOCTL_PHY_LOOPBACK_SETUP		SIOCDEVPRIVATE+8
#define IOCTL_SS_SELF_TEST_MODE_ON_OFF		SIOCDEVPRIVATE+9
#define IOCTL_SS_SELF_TEST_SEND			SIOCDEVPRIVATE+10
#define IOCTL_SS_SELF_TEST_GET_RESULT		SIOCDEVPRIVATE+11
#define IOCTL_SS_SELF_TEST_SET_FRAME		SIOCDEVPRIVATE+12
#define IOCTL_RESET_PHY						SIOCDEVPRIVATE+13
#define IOCTL_RESET_MAC  		          SIOCDEVPRIVATE+14
#define IOCTL_SHOW_DMA_STATUS            SIOCDEVPRIVATE+15
#define GMAC_IRQ	74

extern u32 NVT_Phy_Init_Done;
extern struct timer_list NVT_Phy_InitTimer;

static struct timer_list synopGMAC_cable_unplug_timer;
static u32 GMAC_Power_down; // This global variable is used to indicate the ISR whether the interrupts occured in the process of powering down the mac or not
static u32 do_suspend=0;

#if DEBUG_READ_PHY_REG
u32 *internal_PHY_OFFSET_0120;
u32 *internal_PHY_OFFSET_012C;
u32 *internal_PHY_OFFSET_0130;
#endif

#if 1  // Temportly fix for FPGA verification
extern raw_spinlock_t clock_gen_lock;
#else
DEFINE_RAW_SPINLOCK(clock_gen_lock);
#endif

/*These are the global pointers for their respecive structures*/
extern synopGMACPciNetworkAdapter * synopGMACadapter;
extern synopGMACdevice	          * synopGMACdev;
extern struct net_dev             * synopGMACnetdev;
extern struct semaphore	sem_phy_rw;

static u8 Mac_Addr[6];		// jay hsu : save the current mac address

#ifdef CONFIG_NVT_FASTETH_MAC_NAPI
	#define NVT_NAPI_WEIGHT 64
	// jay hsu : 1 -> napi poll running, otherwise stop
	atomic_t napi_poll = ATOMIC_INIT(0);
	static struct napi_struct nvt_napi;
#endif

static void nvt_wq_detect_linkstatus(u32 notused);
static DECLARE_WORK(priv_work, nvt_wq_detect_linkstatus);
static struct workqueue_struct *plugDetect_Wq;


/*Sample Wake-up frame filter configurations*/

u32 synopGMAC_wakeup_filter_config0[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter1 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x5F5F5F5F,     // For Filter3 CRC is based on 0,1,2,3,4,6,8,9,10,11,12,14,16,17,18,19,20,22,24,25,26,27,28,30 bytes from offset
					0x09000000,     // Filter 0,1,2 are disabled, Filter3 is enabled and filtering applies to only multicast packets
					0x1C000000,     // Filter 0,1,2 (no significance), filter 3 offset is 28 bytes from start of Destination MAC address 
					0x00000000,     // No significance of CRC for Filter0 and Filter1
					0xBDCC0000      // No significance of CRC for Filter2, Filter3 CRC is 0xBDCC
					};
u32 synopGMAC_wakeup_filter_config1[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter1 CRC is not computed may be it is 0x0000
					0x7A7A7A7A,	// For Filter2 CRC is based on 1,3,4,5,6,9,11,12,13,14,17,19,20,21,25,27,28,29,30 bytes from offset
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00010000,     // Filter 0,1,3 are disabled, Filter2 is enabled and filtering applies to only unicast packets
					0x00100000,     // Filter 0,1,3 (no significance), filter 2 offset is 16 bytes from start of Destination MAC address 
					0x00000000,     // No significance of CRC for Filter0 and Filter1
					0x0000A0FE      // No significance of CRC for Filter3, Filter2 CRC is 0xA0FE
					};
u32 synopGMAC_wakeup_filter_config2[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x000000FF,	// For Filter1 CRC is computed on 0,1,2,3,4,5,6,7 bytes from offset
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00000100,     // Filter 0,2,3 are disabled, Filter 1 is enabled and filtering applies to only unicast packets
					0x0000DF00,     // Filter 0,2,3 (no significance), filter 1 offset is 223 bytes from start of Destination MAC address 
					0xDB9E0000,     // No significance of CRC for Filter0, Filter1 CRC is 0xDB9E
					0x00000000      // No significance of CRC for Filter2 and Filter3 
					};

/*
The synopGMAC_wakeup_filter_config3[] is a sample configuration for wake up filter. 
Filter1 is used here
Filter1 offset is programmed to 50 (0x32)
Filter1 mask is set to 0x000000FF, indicating First 8 bytes are used by the filter
Filter1 CRC= 0x7EED this is the CRC computed on data 0x55 0x55 0x55 0x55 0x55 0x55 0x55 0x55

Refer accompanied software DWC_gmac_crc_example.c for CRC16 generation and how to use the same.
*/

u32 synopGMAC_wakeup_filter_config3[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x000000FF,	// For Filter1 CRC is computed on 0,1,2,3,4,5,6,7 bytes from offset
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00000100,     // Filter 0,2,3 are disabled, Filter 1 is enabled and filtering applies to only unicast packets
					0x00003200,     // Filter 0,2,3 (no significance), filter 1 offset is 50 bytes from start of Destination MAC address 
					0x7eED0000,     // No significance of CRC for Filter0, Filter1 CRC is 0x7EED, 
					0x00000000      // No significance of CRC for Filter2 and Filter3 
					};

/* Buffer point for SS self loopback test */
void *SS_selftest_frame;
dma_addr_t SS_selftest_frame_phys;
u32 *external_PHY_reset_dir;
u32 *external_PHY_reset_pull_low;
u32 *external_PHY_reset_pull_high;
u32 inRESET;

/**
 * Function used to detect the cable plugging and unplugging.
 * This function gets scheduled once in every second and polls
 * the PHY register for network cable plug/unplug. Once the 
 * connection is back the GMAC device is configured as per
 * new Duplex mode and Speed of the connection.
 * @param[in] u32 type but is not used currently. 
 * \return returns void.
 * \note This function is tightly coupled with Linux 2.6.xx.
 * \callgraph
 */

static void synopGMAC_linux_cable_unplug_function(u32 notused)
{
	u16 data;

	synopGMACPciNetworkAdapter *adapter = (synopGMACPciNetworkAdapter *)synopGMAC_cable_unplug_timer.data;
	synopGMACdevice            *gmacdev = adapter->synopGMACdev;

	struct net_device *netdev = synopGMACadapter->synopGMACnetdev;

	init_timer(&synopGMAC_cable_unplug_timer);
	synopGMAC_cable_unplug_timer.function = (void *)synopGMAC_linux_cable_unplug_function;
	synopGMAC_cable_unplug_timer.data = (u32) adapter;
	synopGMAC_cable_unplug_timer.expires = jiffies + (HZ/2);

	queue_work(plugDetect_Wq, &priv_work);
	add_timer(&synopGMAC_cable_unplug_timer);
}

static void synopGMAC_linux_powerdown_mac(synopGMACdevice *gmacdev)
{
	TR0("Put the GMAC to power down mode..\n");
	// Disable the Dma engines in tx path
	GMAC_Power_down = 1;	// Let ISR know that Mac is going to be in the power down mode
	synopGMAC_disable_dma_tx(gmacdev);
	plat_delay(10000);		//allow any pending transmission to complete
	// Disable the Mac for both tx and rx
	synopGMAC_tx_disable(gmacdev);
	synopGMAC_rx_disable(gmacdev);
        plat_delay(10000); 		//Allow any pending buffer to be read by host
	//Disable the Dma in rx path
        synopGMAC_disable_dma_rx(gmacdev);

	
	//prepare the gmac for magic packet reception and wake up frame reception
	synopGMAC_magic_packet_enable(gmacdev);
	synopGMAC_write_wakeup_frame_register(gmacdev, synopGMAC_wakeup_filter_config3);

	synopGMAC_wakeup_frame_enable(gmacdev);

	//gate the application and transmit clock inputs to the code. This is not done in this driver :).

	//enable the Mac for reception
	synopGMAC_rx_enable(gmacdev);

	//Enable the assertion of PMT interrupt
	synopGMAC_pmt_int_enable(gmacdev);
	//enter the power down mode
	synopGMAC_power_down_enable(gmacdev);
	return;
}

static void synopGMAC_linux_powerup_mac(synopGMACdevice *gmacdev)
{
	GMAC_Power_down = 0;	// Let ISR know that MAC is out of power down now
#ifdef DEBUG
	if( synopGMAC_is_magic_packet_received(gmacdev))
	{
		TR("GMAC wokeup due to Magic Pkt Received\n");
	}
	if(synopGMAC_is_wakeup_frame_received(gmacdev))
	{
		TR("GMAC wokeup due to Wakeup Frame Received\n");
	}
#endif
	//Disable the assertion of PMT interrupt
	synopGMAC_pmt_int_disable(gmacdev);
	//Enable the mac and Dma rx and tx paths
	synopGMAC_rx_enable(gmacdev);
       	synopGMAC_enable_dma_rx(gmacdev);

	synopGMAC_tx_enable(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);
	return;
}

/* Detect links tatus by work queue */
void nvt_wq_detect_linkstatus(u32 notused){

	u16 data;
	struct net_device *netdev = synopGMACadapter->synopGMACnetdev;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

	/* check phy status */
	synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_STATUS_REG, &data);

	if((data & Mii_Link) == 0)
	{
		if (gmacdev->LinkState == 1 ){
			printk(KERN_CRIT "[NTKETHMAC] Eth Unplug is detected : %08x\n",data);
#ifdef CONFIG_NVT_INTERNAL_PHY
			if(inRESET==0){
				/*WQ, sleep is allowed*/
				synopGMAC_eq_set_function();
				msleep(100);
				synopGMAC_eq_reset_function(1);
			}
#endif
		}
		gmacdev->LinkState = 0;
		gmacdev->DuplexMode = 0;
		gmacdev->Speed = 0;
		gmacdev->LoopBackMode = 0;
#ifdef CONFIG_NVT_INTERNAL_PHY
		synopGMAC_set_nvt_phy_link_trigger_level(gmacdev);
#endif
		netif_carrier_off(synopGMACadapter->synopGMACnetdev);
	}else{

		if ( gmacdev->LinkState == 0 )
			printk(KERN_CRIT "[NTKETHMAC] Eth Plug is detected : %08x\n",data);

		if(!gmacdev->LinkState){
			gmacdev->LinkState = 1;

			if(gmacdev->AutoNgFail){
				synopGMAC_restart_auto_nego(gmacdev);
			}

			synopGMAC_check_phy_init_for_10_100M(gmacdev);

#ifdef CONFIG_NVT_RTL_PHY_SUPPORT
			if(gmacdev->PHY_ID == RTL8201F_PHY_ID){
				synopGMAC_RTL8201F_set_phy_TXC_direction(gmacdev, 1);
				synopGMAC_RTL8201F_set_phy_adjust_RMII_RX_timing(gmacdev, 0xD);
			}
#endif
			if (netdev->flags & IFF_PROMISC) {
				synopGMAC_mac_init(gmacdev, 1);
			}else{
				synopGMAC_mac_init(gmacdev, 0);
			}

			if(do_suspend == 1){
				synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
				synopGMAC_enable_dma_rx(gmacdev);
				synopGMAC_enable_dma_tx(gmacdev);
				do_suspend = 0;
			}

			netif_carrier_on(synopGMACadapter->synopGMACnetdev);
		}
	}
}

/**
  * This sets up the transmit Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Device is interested only after the descriptors are setup. Therefore this function
  * is not included in the device driver API. This function should be treated as an
  * example code to design the descriptor structures for ring mode or chain mode.
  * This function depends on the pcidev structure for allocation consistent dma-able memory in case of linux.
  * This limitation is due to the fact that linux uses pci structure to allocate a dmable memory
  *	- Allocates the memory for the descriptors.
  *	- Initialize the Busy and Next descriptors indices to 0(Indicating first descriptor).
  *	- Initialize the Busy and Next descriptors to first descriptor address.
  * 	- Initialize the last descriptor with the endof ring in case of ring mode.
  *	- Initialize the descriptors in chain mode.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in tx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note This function fails if allocation fails for required number of descriptors in Ring mode, but in chain mode
  *  function returns -ESYNOPGMACNOMEM in the process of descriptor chain creation. once returned from this function
  *  user should for gmacdev->TxDescCount to see how many descriptors are there in the chain. Should continue further
  *  only if the number of descriptors in the chain meets the requirements  
  */

static s32 synopGMAC_setup_tx_desc_queue(synopGMACdevice * gmacdev,struct pci_dev * pcidev,u32 no_of_desc, u32 desc_mode)
{
u32 i;

DmaDesc *first_desc = NULL;
DmaDesc *second_desc = NULL;
dma_addr_t dma_addr;
gmacdev->TxDescCount = 0;

if(desc_mode == RINGMODE){
	TR("Total size of memory required for Tx Descriptors in Ring Mode = 0x%08x\n",((sizeof(DmaDesc) * no_of_desc)));
	
	// jay hsu : alloc memory in kernel
	first_desc = sys_plat_alloc_memory(sizeof(DmaDesc) * no_of_desc);
	dma_addr = (dma_addr_t)first_desc;		// jay hsu : use cache virtual address instead of dma address
	
	
	if(first_desc == NULL){
		TR("Error in Tx Descriptors memory allocation\n");
		return -ESYNOPGMACNOMEM;
	}
	// jay hsu : clear tx descriptors
	memset((char *)first_desc, 0, sizeof(DmaDesc) * no_of_desc);
	gmacdev->TxDescCount = no_of_desc;
	gmacdev->TxDesc      = first_desc;
	gmacdev->TxDescDma   = virt_to_phys((u32 *)dma_addr);		// jay hsu : save the dma address
	
	for(i =0; i < gmacdev -> TxDescCount; i++){
		synopGMAC_tx_desc_init_ring(gmacdev->TxDesc + i, i == gmacdev->TxDescCount-1);
		TR("%02d %08x \n",i, (unsigned int)(gmacdev->TxDesc + i) );
	}
	
	// jay hsu : sync tx init descriptors to dram
	dma_sync_single_for_device(NULL,  virt_to_phys(first_desc),(sizeof(DmaDesc)*no_of_desc), DMA_TO_DEVICE);

}
else{
	
	// jay hsu : alloc memory in kernel
	first_desc = sys_plat_alloc_memory(sizeof(DmaDesc));
	dma_addr = (dma_addr_t)first_desc;		// jay hsu : use cache virtual address instead of dma address
	
	
	if(first_desc == NULL){
		TR("Error in Tx Descriptor Memory allocation in Ring mode\n");
		return -ESYNOPGMACNOMEM;
	}
	gmacdev->TxDesc       = first_desc;
	gmacdev->TxDescDma    = virt_to_phys((u32 *)dma_addr);		// jay hsu : save the dma address

	TR("Tx===================================================================Tx\n");
	first_desc->buffer2   = gmacdev->TxDescDma;
 	first_desc->data2     = (u32)gmacdev->TxDesc;

	gmacdev->TxDescCount = 1;
	
	for(i =0; i <(no_of_desc-1); i++){
		
		// jay hsu : alloc memory in kernel
		second_desc = sys_plat_alloc_memory(sizeof(DmaDesc));
		
		
		if(second_desc == NULL){	
			TR("Error in Tx Descriptor Memory allocation in Chain mode\n");
			return -ESYNOPGMACNOMEM;
		}
		first_desc->buffer2  = virt_to_phys((u32 *)dma_addr);
		first_desc->data2    = (u32)second_desc;
		
		second_desc->buffer2 = gmacdev->TxDescDma;
		second_desc->data2   = (u32)gmacdev->TxDesc;

	        synopGMAC_tx_desc_init_chain(first_desc);
		TR("%02d %08x %08x %08x %08x %08x %08x %08x \n",gmacdev->TxDescCount, (u32)first_desc, first_desc->status, first_desc->length, first_desc->buffer1,first_desc->buffer2, first_desc->data1, first_desc->data2);
		gmacdev->TxDescCount += 1;
		first_desc = second_desc;
	}
		
		synopGMAC_tx_desc_init_chain(first_desc);
		TR("%02d %08x %08x %08x %08x %08x %08x %08x \n",gmacdev->TxDescCount, (u32)first_desc, first_desc->status, first_desc->length, first_desc->buffer1,first_desc->buffer2, first_desc->data1, first_desc->data2);
	TR("Tx===================================================================Tx\n");
}

	gmacdev->TxNext = 0;
	gmacdev->TxBusy = 0;
	gmacdev->TxNextDesc = gmacdev->TxDesc;
	gmacdev->TxBusyDesc = gmacdev->TxDesc;
	gmacdev->BusyTxDesc  = 0; 

	return -ESYNOPGMACNOERR;
}


/**
  * This sets up the receive Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Device is interested only after the descriptors are setup. Therefore this function
  * is not included in the device driver API. This function should be treated as an
  * example code to design the descriptor structures in ring mode or chain mode.
  * This function depends on the pcidev structure for allocation of consistent dma-able memory in case of linux.
  * This limitation is due to the fact that linux uses pci structure to allocate a dmable memory
  *	- Allocates the memory for the descriptors.
  *	- Initialize the Busy and Next descriptors indices to 0(Indicating first descriptor).
  *	- Initialize the Busy and Next descriptors to first descriptor address.
  * 	- Initialize the last descriptor with the endof ring in case of ring mode.
  *	- Initialize the descriptors in chain mode.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in rx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note This function fails if allocation fails for required number of descriptors in Ring mode, but in chain mode
  *  function returns -ESYNOPGMACNOMEM in the process of descriptor chain creation. once returned from this function
  *  user should for gmacdev->RxDescCount to see how many descriptors are there in the chain. Should continue further
  *  only if the number of descriptors in the chain meets the requirements  
  */
static s32 synopGMAC_setup_rx_desc_queue(synopGMACdevice * gmacdev,struct pci_dev * pcidev,u32 no_of_desc, u32 desc_mode)
{
u32 i;

DmaDesc *first_desc = NULL;
DmaDesc *second_desc = NULL;
dma_addr_t dma_addr;
gmacdev->RxDescCount = 0;

if(desc_mode == RINGMODE){
	TR("total size of memory required for Rx Descriptors in Ring Mode = 0x%08x\n",((sizeof(DmaDesc) * no_of_desc)));
	
	// jay hsu : alloc memory in kernel
	first_desc = sys_plat_alloc_memory(sizeof(DmaDesc) * no_of_desc);
	
	if(first_desc == NULL){
		TR("Error in Rx Descriptor Memory allocation in Ring mode\n");
		return -ESYNOPGMACNOMEM;
	}
	
	dma_addr = (dma_addr_t)first_desc;		// jay hsu : use cache virtual address instead of dma address
		
	memset((char *)first_desc, 0, sizeof(DmaDesc) * no_of_desc);
	
	gmacdev->RxDescCount = no_of_desc;
	gmacdev->RxDesc      = first_desc;
	gmacdev->RxDescDma   = virt_to_phys((u32 *)dma_addr);			// jay hsu : save the dma address
	
	for(i =0; i < gmacdev -> RxDescCount; i++){
		synopGMAC_rx_desc_init_ring(gmacdev->RxDesc + i, i == gmacdev->RxDescCount-1);
		TR("%02d %08x \n",i, (unsigned int)(gmacdev->RxDesc + i));

	}
	
	// jay hsu : sync rx init descriptors to dram
	dma_sync_single_for_device(NULL, virt_to_phys(first_desc),(sizeof(DmaDesc)*no_of_desc), DMA_TO_DEVICE);

}
else{
	
	// jay hsu : alloc memory in kernel
	first_desc = sys_plat_alloc_memory(sizeof(DmaDesc));	
	dma_addr = (dma_addr_t)first_desc;		// jay hsu : use cache virtual address instead of dma address
	
	
	if(first_desc == NULL){
		TR("Error in Rx Descriptor Memory allocation in Ring mode\n");
		return -ESYNOPGMACNOMEM;
	}
	gmacdev->RxDesc       = first_desc;
	gmacdev->RxDescDma    = virt_to_phys((u32 *)dma_addr);		// jay hsu : save the dma address

	TR("Rx===================================================================Rx\n");
	first_desc->buffer2   = gmacdev->RxDescDma;
	first_desc->data2     = (u32) gmacdev->RxDesc;

	gmacdev->RxDescCount = 1;
	
	for(i =0; i < (no_of_desc-1); i++){
		
		// jay hsu : alloc memory in kernel
		second_desc = sys_plat_alloc_memory(sizeof(DmaDesc));
		
		if(second_desc == NULL){	
			TR("Error in Rx Descriptor Memory allocation in Chain mode\n");
			return -ESYNOPGMACNOMEM;
		}
		first_desc->buffer2  = virt_to_phys((u32 *)dma_addr);
		first_desc->data2    = (u32)second_desc;
		
		second_desc->buffer2 = gmacdev->RxDescDma;
		second_desc->data2   = (u32)gmacdev->RxDesc;

		synopGMAC_rx_desc_init_chain(first_desc);
		TR("%02d  %08x %08x %08x %08x %08x %08x %08x \n",gmacdev->RxDescCount, (u32)first_desc, first_desc->status, first_desc->length, first_desc->buffer1,first_desc->buffer2, first_desc->data1, first_desc->data2);
		gmacdev->RxDescCount += 1;
		first_desc = second_desc;
	}
                synopGMAC_rx_desc_init_chain(first_desc);
		TR("%02d  %08x %08x %08x %08x %08x %08x %08x \n",gmacdev->RxDescCount, (u32)first_desc, first_desc->status, first_desc->length, first_desc->buffer1,first_desc->buffer2, first_desc->data1, first_desc->data2);
	TR("Rx===================================================================Rx\n");

}

	gmacdev->RxNext = 0;
	gmacdev->RxBusy = 0;
	gmacdev->RxNextDesc = gmacdev->RxDesc;
	gmacdev->RxBusyDesc = gmacdev->RxDesc;

	gmacdev->BusyRxDesc   = 0; 

	return -ESYNOPGMACNOERR;
}

/**
  * This gives up the receive Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Once device's Dma is stopped the memory descriptor memory and the buffer memory deallocation,
  * is completely handled by the operating system, this call is kept outside the device driver Api.
  * This function should be treated as an example code to de-allocate the descriptor structures in ring mode or chain mode
  * and network buffer deallocation.
  * This function depends on the pcidev structure for dma-able memory deallocation for both descriptor memory and the
  * network buffer memory under linux.
  * The responsibility of this function is to 
  *     - Free the network buffer memory if any.
  *	- Fee the memory allocated for the descriptors.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in rx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note No referece should be made to descriptors once this function is called. This function is invoked when the device is closed.
  */
static void synopGMAC_giveup_rx_desc_queue(synopGMACdevice * gmacdev, struct pci_dev *pcidev, u32 desc_mode)
{
u32 i;

DmaDesc *first_desc = NULL;
u32 status;
dma_addr_t dma_addr1;
dma_addr_t dma_addr2;
u32 length1;
u32 length2;
u32 data1;
u32 data2;

if(desc_mode == RINGMODE){
	for(i =0; i < gmacdev -> RxDescCount; i++){
		synopGMAC_get_desc_data(gmacdev->RxDesc + i, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		if((length1 != 0) && (data1 != 0)){
			dev_kfree_skb_any((struct sk_buff *) data1);	// free buffer1
			TR("(Ring mode) rx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		if((length2 != 0) && (data2 != 0)){
			dev_kfree_skb_any((struct sk_buff *) data2);	//free buffer2
			TR("(Ring mode) rx buffer2 %08x of size %d from %d rx descriptor is given back\n",data2, length2, i);
		}
	}
	// jay hsu : free descriptors memory
	sys_plat_free_memory((u32 *)gmacdev->RxDesc);
	
	TR("Memory allocated %08x  for Rx Desriptors (ring) is given back\n",(u32)gmacdev->RxDesc);
}
else{
	TR("rx-------------------------------------------------------------------rx\n");
	first_desc          = gmacdev->RxDesc;
	for(i =0; i < gmacdev -> RxDescCount; i++){
		synopGMAC_get_desc_data(first_desc, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,first_desc->status,first_desc->length,first_desc->buffer1,first_desc->buffer2,first_desc->data1,first_desc->data2);
		if((length1 != 0) && (data1 != 0)){
			dev_kfree_skb_any((struct sk_buff *) data1);	// free buffer1
			TR("(Chain mode) rx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		// jay hsu : free descriptors memory
		sys_plat_free_memory((u32 *)first_desc);	// free descriptors
		
		TR("Memory allocated %08x for Rx Descriptor (chain) at  %d is given back\n",data2,i);

		first_desc = (DmaDesc *)data2;
	}

	TR("rx-------------------------------------------------------------------rx\n");
}
gmacdev->RxDesc    = NULL;
gmacdev->RxDescDma = 0;
return;
}

/**
  * This gives up the transmit Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Once device's Dma is stopped the memory descriptor memory and the buffer memory deallocation,
  * is completely handled by the operating system, this call is kept outside the device driver Api.
  * This function should be treated as an example code to de-allocate the descriptor structures in ring mode or chain mode
  * and network buffer deallocation.
  * This function depends on the pcidev structure for dma-able memory deallocation for both descriptor memory and the
  * network buffer memory under linux.
  * The responsibility of this function is to 
  *     - Free the network buffer memory if any.
  *	- Fee the memory allocated for the descriptors.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in tx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note No reference should be made to descriptors once this function is called. This function is invoked when the device is closed.
  */
static void synopGMAC_giveup_tx_desc_queue(synopGMACdevice * gmacdev,struct pci_dev * pcidev, u32 desc_mode)
{
u32 i;

DmaDesc *first_desc = NULL;
u32 status;
dma_addr_t dma_addr1;
dma_addr_t dma_addr2;
u32 length1;
u32 length2;
u32 data1;
u32 data2;

if(desc_mode == RINGMODE){
	for(i =0; i < gmacdev -> TxDescCount; i++){
		synopGMAC_get_desc_data(gmacdev->TxDesc + i,&status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		if((length1 != 0) && (data1 != 0)){			
			dev_kfree_skb_any((struct sk_buff *) data1);	// free buffer1			
		}
		if((length2 != 0) && (data2 != 0)){			
			dev_kfree_skb_any((struct sk_buff *) data2);	//free buffer2			
		}
	}
	// jay hsu : free descriptors
	sys_plat_free_memory((u32 *)gmacdev->TxDesc);	
	
	
	TR("Memory allocated %08x for Tx Desriptors (ring) is given back\n",(u32)gmacdev->TxDesc);
}
else{
	TR("tx-------------------------------------------------------------------tx\n");
	first_desc          = gmacdev->TxDesc;
	for(i =0; i < gmacdev -> TxDescCount; i++){
		synopGMAC_get_desc_data(first_desc, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,first_desc->status,first_desc->length,first_desc->buffer1,first_desc->buffer2,first_desc->data1,first_desc->data2);
		if((length1 != 0) && (data1 != 0)){
			//pci_unmap_single(pcidev,dma_addr1,0,PCI_DMA_TODEVICE);		// jay hsu
			dev_kfree_skb_any((struct sk_buff *) data2);	// free buffer1
			TR("(Chain mode) tx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		// jay hsu : free descriptors
		sys_plat_free_memory((u32 *)first_desc);
		
		TR("Memory allocated %08x for Tx Descriptor (chain) at  %d is given back\n",data2,i);

		first_desc = (DmaDesc *)data2;
	}
	TR("tx-------------------------------------------------------------------tx\n");

}
gmacdev->TxDesc    = NULL;
gmacdev->TxDescDma = 0;
return;
}


static void synopGMAC_linux_reset_mac(struct net_device *netdev,synopGMACdevice *gmacdev)
{
	s8 status;
	u32 desc_index;
	s32 skblen;
	struct sk_buff *skb;

	inRESET = 1;
	/*Disable all the interrupts*/
	synopGMAC_disable_interrupt_all(gmacdev);
	TR("the synopGMAC interrupt has been disabled\n");

	/*Disable the reception*/	
	synopGMAC_disable_dma_rx(gmacdev);
        synopGMAC_take_desc_ownership_rx(gmacdev);
	TR("the synopGMAC Reception has been disabled\n");

	/*Disable the transmission*/
	synopGMAC_disable_dma_tx(gmacdev);
        synopGMAC_take_desc_ownership_tx(gmacdev);

	netif_stop_queue(netdev);

	synopGMAC_giveup_rx_desc_queue(gmacdev, NULL, RINGMODE);
	synopGMAC_giveup_tx_desc_queue(gmacdev, NULL, RINGMODE);

	synopGMAC_setup_tx_desc_queue(gmacdev,NULL,TRANSMIT_DESC_SIZE, RINGMODE);
	synopGMAC_init_tx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr

	synopGMAC_setup_rx_desc_queue(gmacdev,NULL,RECEIVE_DESC_SIZE, RINGMODE);
	synopGMAC_init_rx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr
	
	do{
		skb = alloc_skb(netdev->mtu + ETHERNET_HEADER + ETHERNET_CRC, GFP_ATOMIC);
		if(skb == NULL){
			TR0("ERROR in skb buffer allocation\n");
			break;
		}

		skblen = skb_tailroom(skb);
		if(skblen > 0){
			status = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0,&desc_index);
		}else{
			status = -1;
		}
		if(status < 0)
			dev_kfree_skb_any(skb);
	}while(status == 0);

#ifndef CONFIG_NVT_INTERNAL_PHY		
		/* External ethernet phy reset */
		*external_PHY_reset_dir |= 1UL << NVT_PHY_RESET_GPIO_BIT;
		*external_PHY_reset_pull_low = 1UL << NVT_PHY_RESET_GPIO_BIT;
		mdelay(1);
		*external_PHY_reset_pull_high = 1UL << NVT_PHY_RESET_GPIO_BIT;
#else
		nvt_internal_PHY_reset(gmacdev);
#endif //CONFIG_NVT_INTERNAL_PHY

#ifdef CONFIG_NVT_RTL_PHY_SUPPORT
	if(gmacdev->PHY_ID == RTL8201F_PHY_ID){
		synopGMAC_RTL8201F_set_phy_TXC_direction(gmacdev, 1);
		synopGMAC_RTL8201F_set_phy_adjust_RMII_RX_timing(gmacdev, 0xD);
	}
#endif
	synopGMAC_clear_interrupt(gmacdev);
	/*
	Disable the interrupts generated by MMC and IPC counters.
	If these are not disabled ISR should be modified accordingly to handle these interrupts.
	*/	
	synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);

	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	synopGMAC_enable_dma_rx(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);
	netif_wake_queue(netdev);
	inRESET = 0;
	return;
}

/**
 * Function to transmit a SS self test packet on the wire.
 * The function prepares the descriptor and enables/resumes the transmission.
 * \return Returns 0 on success and Error code on failure. 
 * \note 
 */
static s32 synopGMAC_SS_self_test_xmit_frames(synopGMACdevice * gmacdev)
{	
	s8 status = 0;
	s32 desc_index = 0;
	u32 offload_needed = 0;	
	unsigned long irq_flags;		// add tx lock support

	if(gmacdev == NULL){
		printk(KERN_CRIT "synopGMAC_SS_self_test_xmit_frames gmacdev == NULL\n");
		return -1;
	}

	if(!gmacdev->ssLoopbackEnable){
		printk(KERN_CRIT "synopGMAC_SS_self_test_xmit_frames ssLoopbackEnable == 0\n");
		return -1;
	}

	spin_lock_irqsave(&gmacdev->tx_lock, irq_flags);	// start spin_lock protection
	status = synopGMAC_set_tx_qptr(gmacdev, SS_selftest_frame_phys, gmacdev->ssLoopbackFrameSize, (u32)SS_selftest_frame,0,0,0,offload_needed,&desc_index);

	if(status < 0){	
		printk(KERN_CRIT "synopGMAC_SS_self_test_xmit_frames status < 0\n");
		spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);	// end spin_lock protection
		return -1;
	}
	
	/*Now force the DMA to start transmission*/	
	synopGMAC_resume_dma_tx(gmacdev);		
	spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);	// end spin_lock protection
		
	return -ESYNOPGMACNOERR;
}

/**
 * Function to Receive a packet from the interface in SS self test mode.
 * @param[in]  
 * \return void.
 * \note This function runs in interrupt context.
 */
static void synopGMAC_SS_self_test_recv_frames(synopGMACdevice * gmacdev)
{
	u32 desc_index;
	
	u32 data1;
	u32 data2;
	u32 len;
	u32 status;
	u32 dma_addr1;
	u32 dma_addr2;
#ifdef ENH_DESC_8W
	u32 ext_status;
	u16 time_stamp_higher;
	u32 time_stamp_high;
	u32 time_stamp_low;
#endif
	u32 i=0;
	s32 skblen;
	struct sk_buff *skb; //This is the pointer to hold the received data
	s8 ret;
	
	/*Handle the Receive Descriptors*/
	do{
#ifdef ENH_DESC_8W
		ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2,&ext_status,&time_stamp_high,&time_stamp_low, &desc_index);
		if(ret == 0){ 
			synopGMAC_TS_read_timestamp_higher_val(gmacdev, &time_stamp_higher);
			TR("S:%08x ES:%08x DA1:%08x d1:%08x DA2:%08x d2:%08x TSH:%08x TSL:%08x TSHW:%08x \n",status,ext_status,dma_addr1, data1,dma_addr2,data2, time_stamp_high,time_stamp_low,time_stamp_higher);
			printk("synopGMAC_SS_self_test_recv_frames desc_index >0\n");
		}
#else
		ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2, &desc_index);
		// debug
		TR("##Handle Received## desc_index %d, dma_addr1 0x%x, data1 0x%x\n", desc_index, (u32)dma_addr1, data1);
#endif
		if(ret == 0 && data1 != 0){
			TR("Received Data at Rx Descriptor %d for skb 0x%08x whose status is %08x\n",desc_index,data1,status);
			/*At first step unmapped the dma address*/
			skb = (struct sk_buff *)data1;
			mb();
			
			if(synopGMAC_is_rx_desc_valid(status)){
				len =  synopGMAC_get_rx_desc_frame_length(status) - 4; //Not interested in Ethernet CRC bytes				
				skb_put(skb,len);

				for(i=0; i<gmacdev->ssLoopbackFrameSize; i++){					
					if((char)*((char*)skb->data+i)^(*((char *)(SS_selftest_frame)+i))){
						break;
					}
				}
				

				if(i<gmacdev->ssLoopbackFrameSize){					
					gmacdev->ssLoopbackErrorPkt++;
				}else{					
					gmacdev->ssLoopbackPassPkt++;
				}

			}else{
				/*Now the present skb should be set free*/
				gmacdev->ssLoopbackErrorPkt++;
				printk(KERN_CRIT "SS self test rx desc is not valid  status = %08x\n",status);
			}
			dev_kfree_skb_any(skb);
			
			//Now lets allocate the skb for the emptied descriptor
			skb = dev_alloc_skb(MAX_ETHERNET_PAYLOAD + ETHERNET_PACKET_EXTRA);
			if(skb == NULL){
				printk(KERN_CRIT "SKB memory allocation failed \n");
				return;
			}

			skblen = skb_tailroom(skb);
			if(skblen > 0){
				ret = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0,&desc_index);
			}else{
				ret = -1;
			}
			if(ret < 0){
				printk(KERN_CRIT "Cannot set Rx Descriptor for skb %08x\n",(u32)skb);
				dev_kfree_skb_any(skb);
			}
		
		}
	}while(ret==0);
}

/**
 * Function to handle housekeeping after a packet is transmitted over the wire.
 * After the transmission of a packet DMA generates corresponding interrupt 
 * (if it is enabled). It takes care of returning the sk_buff to the linux
 * kernel, updating the networking statistics and tracking the descriptors.
 * @param[in] pointer to net_device structure. 
 * \return void.
 * \note This function runs in interrupt context
 */
static void synopGMAC_SS_self_test_transmit_over(synopGMACdevice * gmacdev)
{
	s32 desc_index;
	u32 data1, data2;
	u32 status;
	u32 length1, length2;
	u32 dma_addr1, dma_addr2;
#ifdef ENH_DESC_8W
	u32 ext_status;
	u16 time_stamp_higher;
	u32 time_stamp_high;
	u32 time_stamp_low;
#endif
	unsigned long irq_flags;
	s8 ret;

	/*Handle the transmit Descriptors*/
	do {
		spin_lock_irqsave(&gmacdev->tx_lock, irq_flags);
#ifdef ENH_DESC_8W
		ret = synopGMAC_get_tx_qptr(gmacdev, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2,&ext_status,&time_stamp_high,&time_stamp_low,&desc_index);
	       	synopGMAC_TS_read_timestamp_higher_val(gmacdev, &time_stamp_higher);
#else
		ret = synopGMAC_get_tx_qptr(gmacdev, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2,&desc_index);
#endif
		spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);
				
	} while(ret == 0);
}

/**
 * Function to handle housekeeping after a packet is transmitted over the wire.
 * After the transmission of a packet DMA generates corresponding interrupt 
 * (if it is enabled). It takes care of returning the sk_buff to the linux
 * kernel, updating the networking statistics and tracking the descriptors.
 * @param[in] pointer to net_device structure. 
 * \return void.
 * \note This function runs in interrupt context
 */
static void synop_handle_transmit_over(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	//struct pci_dev *pcidev;		// jay hsu
	u32 desc_index;
	u32 data1, data2;
	u32 status;
	u32 length1, length2;
	u32 dma_addr1, dma_addr2;
#ifdef ENH_DESC_8W
	u32 ext_status;
	u16 time_stamp_higher;
	u32 time_stamp_high;
	u32 time_stamp_low;
#endif
	unsigned long irq_flags;
	s8 ret;	

	TR("%s called \n",__FUNCTION__);	// jay hsu : debug
	
	adapter = netdev_priv(netdev);		// use netdev_priv() @ 20100520
	if(adapter == NULL){
		TR("Unknown Device\n");
		return;
	}
	
	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure is missing\n");
		return;
	}

	if(gmacdev->ssLoopbackEnable){
		synopGMAC_SS_self_test_transmit_over(gmacdev);
		return;
	}	
 	
	/*Handle the transmit Descriptors*/
	do {
		spin_lock_irqsave(&gmacdev->tx_lock, irq_flags); 
#ifdef ENH_DESC_8W
		ret = synopGMAC_get_tx_qptr(gmacdev, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2,&ext_status,&time_stamp_high,&time_stamp_low,&desc_index);
	        synopGMAC_TS_read_timestamp_higher_val(gmacdev, &time_stamp_higher);
#else
		ret = synopGMAC_get_tx_qptr(gmacdev, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2,&desc_index);
#endif
		spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);

		if(ret == 0 && data1 != 0){
			TR("Finished Transmit at Tx Descriptor %d for skb 0x%08x and buffer = %08x whose status is %08x \n", desc_index,data1,dma_addr1,status);
			#ifdef	IPC_OFFLOAD
			if(synopGMAC_is_tx_ipv4header_checksum_error(gmacdev, status)){
			TR("Harware Failed to Insert IPV4 Header Checksum\n");
			}
			if(synopGMAC_is_tx_payload_checksum_error(gmacdev, status)){
			TR("Harware Failed to Insert Payload Checksum\n");
			}
			#endif
		
			dev_kfree_skb_any((struct sk_buff *)data1);

			if(synopGMAC_is_desc_valid(status)){
				adapter->synopGMACNetStats.tx_bytes += length1;
				adapter->synopGMACNetStats.tx_packets++;
				TR("<<tx_bytes>> 0x%lx\n", adapter->synopGMACNetStats.tx_bytes);
				TR("<<tx_packets>> %ld\n", adapter->synopGMACNetStats.tx_packets);
			}
			else {	
				printk(KERN_CRIT "%s: Error in Status %08x\n",__FUNCTION__,status);				
				adapter->synopGMACNetStats.tx_errors++;
				adapter->synopGMACNetStats.tx_aborted_errors += synopGMAC_is_tx_aborted(status);
				adapter->synopGMACNetStats.tx_carrier_errors += synopGMAC_is_tx_carrier_error(status);
			}
		
			// jay hsu @ 20100717 : driver bug fixed. Only count the collision when desc_index isn't -1.
			adapter->synopGMACNetStats.collisions += synopGMAC_get_tx_collision_count(status);
		}	
		
	} while(ret == 0);
	netif_wake_queue(netdev);
}




/**
 * Function to Receive a packet from the interface.
 * After Receiving a packet, DMA transfers the received packet to the system memory
 * and generates corresponding interrupt (if it is enabled). This function prepares
 * the sk_buff for received packet after removing the ethernet CRC, and hands it over
 * to linux networking stack.
 * 	- Updataes the networking interface statistics
 *	- Keeps track of the rx descriptors
 * @param[in] pointer to net_device structure. 
 * \return void.
 * \note This function runs in interrupt context.
 */

static void synop_handle_received_data(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	//struct pci_dev *pcidev;	// jay hsu
	u32 desc_index;
	
	s8 ret;
	u32 data1;
	u32 data2;
	u32 len;
	s32 skblen;
	u32 status;
	u32 dma_addr1;
	u32 dma_addr2;
#ifdef ENH_DESC_8W
	u32 ext_status;
	u16 time_stamp_higher;
	u32 time_stamp_high;
	u32 time_stamp_low;
#endif	
	
	struct sk_buff *skb; //This is the pointer to hold the received data
	
	TR("%s\n",__FUNCTION__);	
	
	adapter = netdev_priv(netdev);		// use netdev_priv() @ 20100520
	if(adapter == NULL){
		TR("Unknown Device\n");
		return;
	}
	
	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure is missing\n");
		return;
	}

	if(gmacdev->ssLoopbackEnable){
		synopGMAC_SS_self_test_recv_frames(gmacdev);
		return;
	}
	
	/*Handle the Receive Descriptors*/
	do{
#ifdef ENH_DESC_8W
		ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2,&ext_status,&time_stamp_high,&time_stamp_low,&desc_index);
		if(ret==0){ 
			synopGMAC_TS_read_timestamp_higher_val(gmacdev, &time_stamp_higher);
			TR("S:%08x ES:%08x DA1:%08x d1:%08x DA2:%08x d2:%08x TSH:%08x TSL:%08x TSHW:%08x \n",status,ext_status,dma_addr1, data1,dma_addr2,data2, time_stamp_high,time_stamp_low,time_stamp_higher);
		}
#else
		ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2,&desc_index);
		// jay hsu : debug
		TR("##Handle Received## desc_index %d, dma_addr1 0x%x, data1 0x%x\n", desc_index, (u32)dma_addr1, data1);		
#endif

		if(ret == 0 && data1 != 0){
			TR("Received Data at Rx Descriptor %d for skb 0x%08x whose status is %08x\n",desc_index,data1,status);
			skb = (struct sk_buff *)data1;

			if ( status & DescSAFilterFail )
			{
				dev_kfree_skb_any(skb);

				// Allocate the skb for the emptied descriptor.
				skb = dev_alloc_skb(netdev->mtu + ETHERNET_PACKET_EXTRA);
				if(skb == NULL){
					printk(KERN_CRIT "%s SKB memory allocation failed \n",__FUNCTION__);
					return;
				}

				adapter->synopGMACNetStats.rx_dropped++;

				skblen = skb_tailroom(skb);
				if(skblen > 0){
					ret = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data),(u32)skblen, (u32)skb,0,0,0, &desc_index);
				}else{
					ret = -1;
				}

				if(ret < 0){
					TR("Cannot set Rx Descriptor for skb %08x\n",(u32)skb);
					printk(KERN_CRIT "Cannot set Rx Descriptor for skb %08x\n",(u32)skb);	// jay hsu : debug test it
					dev_kfree_skb_any(skb);
				}

				return;
			}


			if(synopGMAC_is_rx_desc_valid(status)){
				len =  synopGMAC_get_rx_desc_frame_length(status) - 4; //Not interested in Ethernet CRC bytes
				
				skb_put(skb,len);
			#ifdef IPC_OFFLOAD
				// Now lets check for the IPC offloading
				/*  Since we have enabled the checksum offloading in hardware, lets inform the kernel
				    not to perform the checksum computation on the incoming packet. Note that ip header 
  				    checksum will be computed by the kernel immaterial of what we inform. Similary TCP/UDP/ICMP
				    pseudo header checksum will be computed by the stack. What we can inform is not to perform
				    payload checksum. 		
   				    When CHECKSUM_UNNECESSARY is set kernel bypasses the checksum computation.		    
				*/
				
				TR("Checksum Offloading will be done now\n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				
				#ifdef ENH_DESC_8W
				if(synopGMAC_is_ext_status(gmacdev, status)){ // extended status present indicates that the RDES4 need to be probed
					TR("Extended Status present\n");
					if(synopGMAC_ES_is_IP_header_error(gmacdev,ext_status)){       // IP header (IPV4) checksum error
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR("(EXTSTS)Error in IP header error\n");
					skb->ip_summed = CHECKSUM_NONE;     //Let Kernel compute the checkssum
					}	
					if(synopGMAC_ES_is_rx_checksum_bypassed(gmacdev,ext_status)){   // Hardware engine bypassed the checksum computation/checking
					TR("(EXTSTS)Hardware bypassed checksum computation\n");	
					skb->ip_summed = CHECKSUM_NONE;             // Let Kernel compute the checksum
					}
					if(synopGMAC_ES_is_IP_payload_error(gmacdev,ext_status)){       // IP payload checksum is in error (UDP/TCP/ICMP checksum error)
					TR("(EXTSTS) Error in EP payload\n");	
					skb->ip_summed = CHECKSUM_NONE;             // Let Kernel compute the checksum
					}				
				}
				else{ // No extended status. So relevant information is available in the status itself
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxNoChkError ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 4>  \n");
					skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass computing the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR(" Error in 16bit IPV4 Header Checksum <Chk Status = 6>  \n");
					skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass the TCP/UDP checksum computation
					}				
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxLenLT600 ){
					TR("IEEE 802.3 type frame with Length field Lesss than 0x0600 <Chk Status = 0> \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrPayLoadChkBypass ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 1>\n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxChkBypass ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 3>  \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxPayLoadChkError ){
					TR(" TCP/UDP payload checksum Error <Chk Status = 5>  \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR(" Both IP header and Payload Checksum Error <Chk Status = 7>  \n");
					skb->ip_summed = CHECKSUM_NONE;	        //Let Kernel compute the Checksum
					}
				}
				#else	
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxNoChkError ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 4>  \n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass computing the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
				//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
				TR(" Error in 16bit IPV4 Header Checksum <Chk Status = 6>  \n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass the TCP/UDP checksum computation
				}				
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxLenLT600 ){
				TR("IEEE 802.3 type frame with Length field Lesss than 0x0600 <Chk Status = 0> \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrPayLoadChkBypass ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 1>\n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxChkBypass ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 3>  \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxPayLoadChkError ){
				TR(" TCP/UDP payload checksum Error <Chk Status = 5>  \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
				//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
				TR(" Both IP header and Payload Checksum Error <Chk Status = 7>  \n");
				skb->ip_summed = CHECKSUM_NONE;	        //Let Kernel compute the Checksum
				}
				
				#endif
			#endif //IPC_OFFLOAD	
				skb->dev = netdev;
				skb->protocol = eth_type_trans(skb, netdev);
				netif_rx(skb);
				netdev->last_rx = jiffies;
				adapter->synopGMACNetStats.rx_packets++;
				adapter->synopGMACNetStats.rx_bytes += len;
			}
			else{
				/*Now the present skb should be set free*/
				dev_kfree_skb_any(skb);
				printk(KERN_CRIT "\033[1;31m[NTKETHMAC_ERROR] %s: Descriptor is not vaild status = %08x , next Desc index=%d\033[0m\n",__FUNCTION__,status,desc_index);
				
				

				adapter->synopGMACNetStats.rx_errors++;
				if(synopGMAC_is_rx_frame_collision(status)){
					adapter->synopGMACNetStats.collisions++;					
				}
				if(synopGMAC_is_rx_crc(status)){
					adapter->synopGMACNetStats.rx_crc_errors++;					
				}
				if(synopGMAC_is_frame_dribbling_errors(status)){
					adapter->synopGMACNetStats.rx_frame_errors++;					
				}
				if(synopGMAC_is_rx_frame_length_errors(status))
				{
					adapter->synopGMACNetStats.rx_length_errors++;					
				}
			}
			
			//Now lets allocate the skb for the emptied descriptor
			skb = dev_alloc_skb(netdev->mtu + ETHERNET_PACKET_EXTRA);
			if(skb == NULL){
				printk(KERN_CRIT "%s SKB memory allocation failed \n",__FUNCTION__);
				adapter->synopGMACNetStats.rx_dropped++;
				return;
			}			
		

			skblen=skb_tailroom(skb);
			if(skblen > 0){
				ret = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data),(u32)skblen, (u32)skb,0,0,0, &desc_index);
			}else{
				ret = -1;
			}

			if(ret < 0){
				TR("Cannot set Rx Descriptor for skb %08x\n",(u32)skb);
				printk(KERN_CRIT "Cannot set Rx Descriptor for skb %08x\n",(u32)skb);	// jay hsu : debug test it
				dev_kfree_skb_any(skb);
			}
		
		}
	}while(ret == 0);

}


/**
 * Interrupt service routing.
 * This is the function registered as ISR for device interrupts.
 * @param[in] interrupt number. 
 * @param[in] void pointer to device unique structure (Required for shared interrupts in Linux).
 * @param[in] pointer to pt_regs (not used).
 * \return Returns IRQ_NONE if not device interrupts IRQ_HANDLED for device interrupts.
 * \note This function runs in interrupt context
 *
 */

static irqreturn_t synopGMAC_intr_handler(int irq, void *dev_id)
{       
	/*Kernels passes the netdev structure in the dev_id. So grab it*/
        struct net_device *netdev;
        synopGMACPciNetworkAdapter *adapter;
        synopGMACdevice * gmacdev;
        u32 interrupt,dma_status_reg;
	s8 status;
	u32 desc_index;
	s32 skblen;	

	TR("%s called \n",__FUNCTION__);	// jay hsu : debug
	
        netdev  = (struct net_device *) dev_id;
        if(netdev == NULL){
                TR("Unknown Device\n");
                return -1;
        }
        adapter  = netdev_priv(netdev);		// use netdev_priv() @ 20100520
        if(adapter == NULL){
                TR("Adapter Structure Missing\n");
                return -1;
        }

        gmacdev = adapter->synopGMACdev;
        if(gmacdev == NULL){
                TR("GMAC device structure Missing\n");
                return -1;
        }
	/*Read the Dma interrupt status to know whether the interrupt got generated by our device or not*/
	dma_status_reg = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaStatus);
	
	if(dma_status_reg == 0)
		return IRQ_NONE;

        synopGMAC_disable_interrupt_all(gmacdev);

       	TR("%s:Dma Status Reg: 0x%08x\n",__FUNCTION__,dma_status_reg);
	
	if(dma_status_reg & GmacPmtIntr){
		TR("%s:: Interrupt due to PMT module\n",__FUNCTION__);
		synopGMAC_linux_powerup_mac(gmacdev);
	}
	
	if(dma_status_reg & GmacMmcIntr){
		TR("%s:: Interrupt due to MMC module\n",__FUNCTION__);
		TR("%s:: synopGMAC_rx_int_status = %08x\n",__FUNCTION__,synopGMAC_read_mmc_rx_int_status(gmacdev));
		TR("%s:: synopGMAC_tx_int_status = %08x\n",__FUNCTION__,synopGMAC_read_mmc_tx_int_status(gmacdev));
	}

	if(dma_status_reg & GmacLineIntfIntr){
		TR("%s:: Interrupt due to GMAC LINE module\n",__FUNCTION__);
	}

	/*Now lets handle the DMA interrupts*/  
        interrupt = synopGMAC_get_interrupt_type(gmacdev);
       	TR("%s:Interrupts to be handled: 0x%08x\n",__FUNCTION__,interrupt);

        if(interrupt & synopGMACDmaError){
		TR("%s::Fatal Bus Error Inetrrupt Seen\n",__FUNCTION__);
		synopGMAC_disable_dma_tx(gmacdev);
                synopGMAC_disable_dma_rx(gmacdev);
                
		synopGMAC_take_desc_ownership_tx(gmacdev);
		synopGMAC_take_desc_ownership_rx(gmacdev);
		
		synopGMAC_init_tx_rx_desc_queue(gmacdev);
		
		synopGMAC_reset(gmacdev);//reset the DMA engine and the GMAC ip
		
		synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, Mac_Addr);
		synopGMAC_set_mac_addr(gmacdev,GmacAddr1High,GmacAddr1Low, Mac_Addr);

		synopGMAC_dma_bus_mode_init(gmacdev,DmaFixedBurstEnable| DmaBurstLength8 | DmaDescriptorSkip6 );
	 	synopGMAC_dma_control_init(gmacdev,DmaStoreAndForward);	
		synopGMAC_init_rx_desc_base(gmacdev);
		synopGMAC_init_tx_desc_base(gmacdev);

		if (netdev->flags & IFF_PROMISC) {
			synopGMAC_mac_init(gmacdev, 1);
		}else{
			synopGMAC_mac_init(gmacdev, 0);
		}

		synopGMAC_enable_dma_rx(gmacdev);
		synopGMAC_enable_dma_tx(gmacdev);

        }

	if(interrupt & synopGMACDmaRxNormal){
		TR("%s:: Rx Normal \n", __FUNCTION__);
			
#ifdef CONFIG_NVT_FASTETH_MAC_NAPI
		if (likely(napi_schedule_prep(&nvt_napi))) {
			atomic_set(&napi_poll, 1);
			__napi_schedule(&nvt_napi);
		}		
#else
		synop_handle_received_data(netdev);
#endif
	}

        if(interrupt & synopGMACDmaRxAbnormal){
	        TR("%s::Abnormal Rx Interrupt Seen\n",__FUNCTION__);
	        printk("[NTKETHMAC] : Abnormal Rx, Do Flow Control!!\n");
	
			synopGMAC_tx_activate_flow_control(gmacdev);	// jay hsu : need deactive flow control in half-duplex @ 20100602
	
	}


        if(interrupt & synopGMACDmaRxStopped){
        	TR("%s::Receiver stopped seeing Rx interrupts\n",__FUNCTION__); //Receiver gone in to stopped state
		#if 1
	        if(GMAC_Power_down == 0){	// If Mac is not in powerdown
		adapter->synopGMACNetStats.rx_over_errors++;
		do{
			struct sk_buff *skb = alloc_skb(netdev->mtu + ETHERNET_HEADER + ETHERNET_CRC, GFP_ATOMIC);
			if(skb == NULL){
				TR("%s::ERROR in skb buffer allocation Better Luck Next time\n",__FUNCTION__);
				break;
			}
			
			skblen = skb_tailroom(skb);
			if(skblen > 0){
				status = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0, &desc_index);
			}else{
				status = -1;
			}
			TR("%s::Set Rx Descriptor no %08x for skb %08x \n",__FUNCTION__,desc_index,(u32)skb);
			if(status < 0)
				dev_kfree_skb_any(skb);//changed from dev_free_skb. If problem check this again--manju
		
		}while(status == 0);
		
		synopGMAC_enable_dma_rx(gmacdev);
		}
		#endif
	}

	if(interrupt & synopGMACDmaTxNormal){
		//xmit function has done its job
		TR("%s::Finished Normal Transmission \n",__FUNCTION__);
		synop_handle_transmit_over(netdev);//Do whatever you want after the transmission is over

		
	}

        if(interrupt & synopGMACDmaTxAbnormal){
		TR("%s::Abnormal Tx Interrupt Seen\n",__FUNCTION__);
		printk("%s::Abnormal Tx Interrupt Seen\n",__FUNCTION__);
		#if 1
	       if(GMAC_Power_down == 0){	// If Mac is not in powerdown
                synop_handle_transmit_over(netdev);
		}
		#endif
	}


	if(interrupt & synopGMACDmaTxStopped){
		TR("%s::Transmitter stopped sending the packets\n",__FUNCTION__);
		printk("%s::Transmitter stopped sending the packets\n",__FUNCTION__);
		#if 1
	       if(GMAC_Power_down == 0){	// If Mac is not in powerdown
		synopGMAC_disable_dma_tx(gmacdev);
                synopGMAC_take_desc_ownership_tx(gmacdev);
		
		synopGMAC_enable_dma_tx(gmacdev);
		netif_wake_queue(netdev);
		TR("%s::Transmission Resumed\n",__FUNCTION__);
		}
		#endif
	}

#ifdef CONFIG_NVT_FASTETH_MAC_NAPI
	if ( atomic_read(&napi_poll)==1 )
		// jay hsu : ignore the rx complete
		synopGMAC_enable_interrupt(gmacdev,DmaIntEnable&(~DmaIntRxNormMask));
	else
		/* Enable the interrrupt before returning from ISR*/
		synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
#else
    	/* Enable the interrrupt before returning from ISR*/
		synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
#endif	

	return IRQ_HANDLED;
}


/**
 * Function used when the interface is opened for use.
 * We register synopGMAC_linux_open function to linux open(). Basically this 
 * function prepares the the device for operation . This function is called whenever ifconfig (in Linux)
 * activates the device (for example "ifconfig eth0 up"). This function registers
 * system resources needed 
 * 	- Attaches device to device specific structure
 * 	- Programs the MDC clock for PHY configuration
 * 	- Check and initialize the PHY interface 
 *	- ISR registration
 * 	- Setup and initialize Tx and Rx descriptors
 *	- Initialize MAC and DMA
 *	- Allocate Memory for RX descriptors (The should be DMAable)
 * 	- Initialize one second timer to detect cable plug/unplug
 *	- Configure and Enable Interrupts
 *	- Enable Tx and Rx
 *	- start the Linux network queue interface
 * @param[in] pointer to net_device structure. 
 * \return Returns 0 on success and error status upon failure.
 * \callgraph
 */

s32 synopGMAC_linux_open(struct net_device *netdev)
{
	//u8 mac_addr0[6] = DEFAULT_MAC_ADDRESS;
	s8 status = 0;
	u32 Phy_Init_try_count = NVT_PHY_TRY_COUNT;
	u32 desc_index =0;
	s32 retval = 0;
	s32 ijk;
	s32 count,skblen;
#ifdef USE_DEVICE_TREE
	#ifndef CONFIG_NVT_INTERNAL_PHY
	u32 sel_RMII_reg_val;
	#endif
#endif // USE_DEVICE_TREE

#if DEBUG_READ_PHY_REG
	s32 i;
#endif


	struct sk_buff *skb;
        synopGMACPciNetworkAdapter *adapter;
        synopGMACdevice * gmacdev;
	TR0("%s called \n",__FUNCTION__);
	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);	// use netdev_priv() @ 2010520
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;


	/*Lets read the version of ip in to device structure*/	
	synopGMAC_read_version(gmacdev);
	
	// jay hsu : set mac address again
	synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, Mac_Addr);
	synopGMAC_set_mac_addr(gmacdev,GmacAddr1High,GmacAddr1Low, Mac_Addr);
	
	synopGMAC_get_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 
	/*Now set the broadcast address*/	
	for(ijk = 0; ijk <6; ijk++){
	netdev->broadcast[ijk] = 0xff;
	}

	for(ijk = 0; ijk <6; ijk++){
	TR("netdev->dev_addr[%d] = %02x and netdev->broadcast[%d] = %02x\n",ijk,netdev->dev_addr[ijk],ijk,netdev->broadcast[ijk]);
	}
	/*Check for Phy initialization*/
	synopGMAC_set_mdc_clk_div(gmacdev,g_MDC_CLOCK);
	gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);

	gmacdev->ssLoopbackEnable=0;

#ifndef CONFIG_NVT_INTERNAL_PHY

#ifdef USE_DEVICE_TREE
	sel_RMII_reg_val = readl(gmacdev->RmiiModeCtrlReg);
	sel_RMII_reg_val  |= 0x80000000;
	writel(sel_RMII_reg_val, gmacdev->RmiiModeCtrlReg);
#else //not USE_DEVICE_TREE
	ioremap_OR_value(MAC_RMII_MII_SEL_ADDR,0x80000000);
	
	/* External ethernet phy reset */
	ioremap_OR_value(NVT_PHY_RESET_GPIO_ADDR + 0x8,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
	ioremap_write(NVT_PHY_RESET_GPIO_ADDR,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
	mdelay(1);
	ioremap_write(NVT_PHY_RESET_GPIO_ADDR + 0x4, (u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
	mdelay(100);
#endif //USE_DEVICE_TREE

#endif //CONFIG_NVT_INTERNAL_PHY

	while(Phy_Init_try_count-- > 0)
	{
		if (NVT_Phy_Init_Done)
			break;

		msleep(1);
	}
	if ( Phy_Init_try_count <= 0 ) {
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_ERROR]: Reset Digital Phy is Timeout!\033[0m\n");
	}

	// Jay Hsu : check phy id until phy reset procedure done
	count = 100;
	while (count--)
	{        
#ifdef CONFIG_NVT_MICREL_PHY_SUPPORT
		if ( (synopGMAC_get_phy_id(gmacdev) == MICREL_PHY_ID1) ||  (synopGMAC_get_phy_id(gmacdev) == MICREL_PHY_ID2) )
#elif defined CONFIG_NVT_RTL_PHY_SUPPORT
		if ( (synopGMAC_get_phy_id(gmacdev) == RTL8201E_PHY_ID) || (synopGMAC_get_phy_id(gmacdev) == RTL8201F_PHY_ID))
#elif defined CONFIG_NVT_ICPLUS_PHY_SUPPORT
		if ( (synopGMAC_get_phy_id(gmacdev) == ICPLUS101ALF_PHY_ID ) )
#elif defined CONFIG_NVT_INTERNAL_PHY
		if ( (synopGMAC_get_phy_id(gmacdev) == NVT_INTERNAL_PHY_ID ) )
#endif		
		{
			printk("[NTKETHMAC] : %s Ethernet PHY ready\n",NVT_PHY_VENDOR);
			break;
		}
		mdelay(10);
	}
	if ( count < 0 )
	{
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_ERROR]: Not %s PHY can't initialize PHY ID = %08x\033[0m\n",NVT_PHY_VENDOR,gmacdev->PHY_ID);

	#if DEBUG_READ_PHY_REG
		*internal_PHY_OFFSET_0120 = 0x00000001;

		for (i=0; i<= 100; i++) {
			if ( ((*internal_PHY_OFFSET_0120 >> 4) & 0x1) == 1 ) {
				printk(KERN_CRIT "\033[1;31m[NTKETHMAC_DEBUG]: PHY reg_120= %08x\033[0m\n", *internal_PHY_OFFSET_0120);
				break;
			}
			mdelay(1);
		}

		mdelay(10);
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_DEBUG]: PHY reg_12C= %08x\033[0m\n", *internal_PHY_OFFSET_012C);
		printk(KERN_CRIT "\033[1;31m[NTKETHMAC_DEBUG]: PHY reg_130= %08x\033[0m\n", *internal_PHY_OFFSET_0130);
	#endif

	 	return ESYNOPGMACPHYERR;
	}
	
	// jay hsu : check phy init speed for our Micrel phy
	synopGMAC_check_phy_init_for_10_100M(gmacdev);


#ifdef CONFIG_NVT_RTL_PHY_SUPPORT
	if(gmacdev->PHY_ID == RTL8201E_PHY_ID){
		synopGMAC_get_phy_test_reg(gmacdev);
		synopGMAC_get_phy_LBREM_reg(gmacdev);
	}else if(gmacdev->PHY_ID == RTL8201F_PHY_ID){
		synopGMAC_RTL8201F_set_phy_TXC_direction(gmacdev, 1);
		synopGMAC_RTL8201F_set_phy_adjust_RMII_RX_timing(gmacdev, 0xD);
	}
#endif

	// use new IRQ definition @ 20100520
#ifdef USE_DEVICE_TREE
	if(request_irq (gmacdev->IRQNum, synopGMAC_intr_handler, IRQF_DISABLED, netdev->name,netdev))
#else
	if(request_irq (GMAC_IRQ, synopGMAC_intr_handler, IRQF_DISABLED, netdev->name,netdev))
#endif
	{
 		TR0("Error in request_irq\n");
		goto error_in_irq;	
	}

	/*Set up the tx and rx descriptor queue/ring*/
	synopGMAC_setup_tx_desc_queue(gmacdev,NULL,TRANSMIT_DESC_SIZE, RINGMODE);
	synopGMAC_init_tx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr


	synopGMAC_setup_rx_desc_queue(gmacdev,NULL,RECEIVE_DESC_SIZE, RINGMODE);
	synopGMAC_init_rx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr

	// jay hsu : use burst length 8
	synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength8 | DmaDescriptorSkip6);	// jay hsu : skip 6x8 bytes to get next descriptors
	
	// jay hsu : don't use Operate on second frame
	synopGMAC_dma_control_init(gmacdev,DmaStoreAndForward |DmaRxThreshCtrl128);

	if (netdev->flags & IFF_PROMISC) {
		synopGMAC_mac_init(gmacdev, 1);
	}else{
		synopGMAC_mac_init(gmacdev, 0);
	}


	synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation
	
	#ifdef IPC_OFFLOAD
	/*IPC Checksum offloading is enabled for this driver. Should only be used if Full Ip checksumm offload engine is configured in the hardware*/
	synopGMAC_enable_rx_chksum_offload(gmacdev);  	//Enable the offload engine in the receive path
	synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev); // This is default configuration, DMA drops the packets if error in encapsulated ethernet payload
							// The FEF bit in DMA control register is configured to 0 indicating DMA to drop the errored frames.
	/*Inform the Linux Networking stack about the hardware capability of checksum offloading*/
	netdev->features = NETIF_F_HW_CSUM;
	#endif
	
     do{
		skb = alloc_skb(netdev->mtu + ETHERNET_HEADER + ETHERNET_CRC, GFP_ATOMIC);
		if(skb == NULL){
			TR0("ERROR in skb buffer allocation\n");
			synopGMAC_giveup_rx_desc_queue(gmacdev, NULL, RINGMODE);
			synopGMAC_giveup_tx_desc_queue(gmacdev, NULL, RINGMODE);
			return -ESYNOPGMACNOMEM;
		}
				
		skblen = skb_tailroom(skb);
		if(skblen > 0){
			status = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0,&desc_index);
		}else{
			printk("Set RX descriptor fail\n");
			status = -1;
		}

		if(status < 0)
			dev_kfree_skb_any(skb);
			
	}while(status == 0);
	

	TR("Setting up the cable unplug timer\n");
	init_timer(&synopGMAC_cable_unplug_timer);
	synopGMAC_cable_unplug_timer.function = (void *)synopGMAC_linux_cable_unplug_function;
	synopGMAC_cable_unplug_timer.data = (u32) adapter;
	synopGMAC_cable_unplug_timer.expires = jiffies + (HZ/2);
	add_timer(&synopGMAC_cable_unplug_timer);

	synopGMAC_clear_interrupt(gmacdev);
	/*
	Disable the interrupts generated by MMC and IPC counters.
	If these are not disabled ISR should be modified accordingly to handle these interrupts.
	*/	
	synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);

	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	synopGMAC_enable_dma_rx(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);

        /* Power up MAC */
	synopGMAC_power_down_disable(gmacdev);
	
	if( gmacdev->LinkState == 1){ 
		netif_carrier_on(netdev); 
	}else{
		netif_carrier_off(netdev);
	}
	
	netif_start_queue(netdev);

#ifdef	CONFIG_NVT_FASTETH_MAC_NAPI
	napi_enable(&nvt_napi);	
#endif

	return retval;

error_in_irq:
	/*Lets free the allocated memory*/
	plat_free_memory(gmacdev);
	return -ESYNOPGMACBUSY;
}

/**
 * Function used when the interface is closed.
 *
 * This function is registered to linux stop() function. This function is 
 * called whenever ifconfig (in Linux) closes the device (for example "ifconfig eth0 down").
 * This releases all the system resources allocated during open call.
 * system resources int needs 
 * 	- Disable the device interrupts
 * 	- Stop the receiver and get back all the rx descriptors from the DMA
 * 	- Stop the transmitter and get back all the tx descriptors from the DMA 
 * 	- Stop the Linux network queue interface
 *	- Free the irq (ISR registered is removed from the kernel)
 * 	- Release the TX and RX descripor memory
 *	- De-initialize one second timer rgistered for cable plug/unplug tracking
 * @param[in] pointer to net_device structure. 
 * \return Returns 0 on success and error status upon failure.
 * \callgraph
 */

s32 synopGMAC_linux_close(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
        synopGMACdevice * gmacdev;
	
	TR0("%s\n",__FUNCTION__);
	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);	// use netdev_priv() @ 20100520
	if(adapter == NULL){
		TR0("OOPS adapter is null\n");
		return -1;
	}

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR0("OOPS gmacdev is null\n");
		return -1;
	}

	/*Disable all the interrupts*/
	synopGMAC_disable_interrupt_all(gmacdev);
	TR("the synopGMAC interrupt has been disabled\n");

	/*Disable the reception*/	
	synopGMAC_disable_dma_rx(gmacdev);
        synopGMAC_take_desc_ownership_rx(gmacdev);
	TR("the synopGMAC Reception has been disabled\n");

	/*Disable the transmission*/
	synopGMAC_disable_dma_tx(gmacdev);
        synopGMAC_take_desc_ownership_tx(gmacdev);

#ifdef CONFIG_NVT_FASTETH_MAC_NAPI	
	napi_disable(&nvt_napi);	
#endif	
	
	TR("the synopGMAC Transmission has been disabled\n");
	netif_stop_queue(netdev);
	
	// jay hsu : free GMAC irq	
#ifdef USE_DEVICE_TREE	
	free_irq(gmacdev->IRQNum, netdev);
#else
	free_irq(GMAC_IRQ, netdev);
#endif	
	TR("the synopGMAC interrupt handler has been removed\n");
	
	/*Free the Rx Descriptor contents*/
	TR("Now calling synopGMAC_giveup_rx_desc_queue \n");
	synopGMAC_giveup_rx_desc_queue(gmacdev, NULL, RINGMODE);
	
	TR("Now calling synopGMAC_giveup_tx_desc_queue \n");
	synopGMAC_giveup_tx_desc_queue(gmacdev, NULL, RINGMODE);
	
	
	TR("Freeing the cable unplug timer\n");	

	del_timer(&NVT_Phy_InitTimer);

	del_timer(&synopGMAC_cable_unplug_timer);

	return -ESYNOPGMACNOERR;

}

/**
 * Function to transmit a given packet on the wire.
 * Whenever Linux Kernel has a packet ready to be transmitted, this function is called.
 * The function prepares a packet and prepares the descriptor and 
 * enables/resumes the transmission.
 * @param[in] pointer to sk_buff structure. 
 * @param[in] pointer to net_device structure.
 * \return Returns 0 on success and Error code on failure. 
 * \note structure sk_buff is used to hold packet in Linux networking stacks.
 */
s32 synopGMAC_linux_xmit_frames(struct sk_buff *skb, struct net_device *netdev)
{	
	s8 status = 0;
	u32 desc_index;
	u32 offload_needed = 0;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	
	unsigned long irq_flags;		// jay hsu : add tx lock support
	
	TR("%s called \n",__FUNCTION__);
	if(skb == NULL){
		TR0("skb is NULL What happened to Linux Kernel? \n ");
		return -1;
	}
	
	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);	// use netdev_prvi() @ 20100520
	if(adapter == NULL)
		return -1;

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(gmacdev == NULL)
		return -1;

	if(gmacdev->ssLoopbackEnable){
		return NETDEV_TX_BUSY;
	}

	
		// jay hsu : 2.6.30 use UDP-lite, then skb->ip_summed will set by CHECKSUM_PARTIAL
		if(skb->ip_summed == CHECKSUM_COMPLETE || skb->ip_summed == CHECKSUM_PARTIAL){
		/*	
		   In Linux networking, if kernel indicates skb->ip_summed = CHECKSUM_HW, then only checksum offloading should be performed
		   Make sure that the OS on which this code runs have proper support to enable offloading.
		*/
		offload_needed = 0x00000001;
		}


	// jay hsu : debug
	TR("##send frame## skb data 0x%x, len %d\n", (u32)skb->data, skb->len);
	TR("offload needed %d\n", offload_needed);
	
	spin_lock_irqsave(&gmacdev->tx_lock, irq_flags);	// jay hsu @ 20100727 : start spin_lock protection
	status = synopGMAC_set_tx_qptr(gmacdev, virt_to_phys(skb->data), skb->len, (u32)skb,0,0,0,offload_needed,&desc_index);	
	
	if(status < 0){
		printk("%s No More Free Tx Descriptors\n",__FUNCTION__);	


		// jay hsu : fix the errno @ 20100727
		spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);
		synopGMAC_linux_reset_mac(netdev,gmacdev);
		return NETDEV_TX_BUSY;
	}
	
	/*Now force the DMA to start transmission*/	
	synopGMAC_resume_dma_tx(gmacdev);	
	spin_unlock_irqrestore(&gmacdev->tx_lock, irq_flags);
	netdev->trans_start = jiffies;	
	
	return -ESYNOPGMACNOERR;
}

/**
 * Function provides the network interface statistics.
 * Function is registered to linux get_stats() function. This function is 
 * called whenever ifconfig (in Linux) asks for networkig statistics
 * (for example "ifconfig eth0").
 * @param[in] pointer to net_device structure. 
 * \return Returns pointer to net_device_stats structure.
 * \callgraph
 */
struct net_device_stats *  synopGMAC_linux_get_stats(struct net_device *netdev)
{
TR("%s called \n",__FUNCTION__);
return( &(((synopGMACPciNetworkAdapter *)(netdev_priv(netdev)))->synopGMACNetStats) );
}

/**
 * Function to set multicast and promiscous mode.
 * @param[in] pointer to net_device structure. 
 * \return returns void.
 */
void synopGMAC_linux_set_multicast_list(struct net_device *netdev)
{
TR("%s called \n",__FUNCTION__);
//todo Function not yet implemented.
return;
}

/**
 * Function to set ethernet address of the NIC.
 * @param[in] pointer to net_device structure. 
 * @param[in] pointer to an address structure. 
 * \return Returns 0 on success Errorcode on failure.
 */
s32 synopGMAC_linux_set_mac_address(struct net_device *netdev, void * macaddr)
{

synopGMACPciNetworkAdapter *adapter = NULL;
synopGMACdevice * gmacdev = NULL;
struct sockaddr *addr = macaddr;

//adapter = (synopGMACPciNetworkAdapter *) netdev->priv;
adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);
if(adapter == NULL)
	return -1;

gmacdev = adapter->synopGMACdev;
if(gmacdev == NULL)
	return -1;

if(!is_valid_ether_addr(addr->sa_data))
	return -EADDRNOTAVAIL;
	
	// Jay Hsu @ 20100628 : only set mac address when net device is down. 
	if ( netif_running(netdev) )
		return -EBUSY;

	// jay hsu : save current mac address in dram
	memcpy(Mac_Addr, addr->sa_data, sizeof(Mac_Addr));

	synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, addr->sa_data);
	synopGMAC_set_mac_addr(gmacdev,GmacAddr1High,GmacAddr1Low, addr->sa_data);

	synopGMAC_get_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 

TR("%s called \n",__FUNCTION__);
return 0;
}

/**
 * Function to change the Maximum Transfer Unit.
 * @param[in] pointer to net_device structure. 
 * @param[in] New value for maximum frame size.
 * \return Returns 0 on success Errorcode on failure.
 */
s32 synopGMAC_linux_change_mtu(struct net_device *netdev, s32 newmtu)
{
TR("%s called \n",__FUNCTION__);
//todo Function not yet implemented.
return 0;

}

/**
 * IOCTL interface.
 * This function is mainly for debugging purpose.
 * This provides hooks for Register read write, Retrieve descriptor status
 * and Retreiving Device structure information.
 * @param[in] pointer to net_device structure. 
 * @param[in] pointer to ifreq structure.
 * @param[in] ioctl command. 
 * \return Returns 0 on success Error code on failure.
 */
s32 synopGMAC_linux_do_ioctl(struct net_device *netdev, struct ifreq *ifr, s32 cmd)
{
	s32 retval = 0;
	u32 currdescaddr;
	u16 temp_data = 0;
	synopGMACPciNetworkAdapter *adapter = NULL;
	synopGMACdevice * gmacdev = NULL;

	struct ifr_data_struct
	{
		u32 unit;
		u32 addr;
		u32 data;
		u32 len;
	} *req;

	struct mii_ioctl_data *mii_data;	// Jay Hsu @ 20100628 : for mii ioctl command
	DmaDesc * currdesc;
	
	if(netdev == NULL)
		return -1;
	if(ifr == NULL)
		return -1;

	req = (struct ifr_data_struct *)ifr->ifr_data;

	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);
	if(adapter == NULL)
		return -1;

	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL)
		return -1;
	

		// Jay Hsu @ 20100628 : check if the net device is up.
		if ( !netif_running(netdev) )
			return -ENODEV;
	switch(cmd)
	{
		case IOCTL_READ_REGISTER:		//IOCTL for reading IP registers : Read Registers
			if      (req->unit == 0)	// Read Mac Register
				req->data = synopGMACReadReg((u32 *)gmacdev->MacBase,req->addr);
			else if (req->unit == 1)	// Read DMA Register
				req->data = synopGMACReadReg((u32 *)gmacdev->DmaBase,req->addr);
			else if (req->unit == 2){	// Read Phy Register
				retval = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,&temp_data);
				req->data = (u32)temp_data;
#ifdef DEBUG
				if(retval != -ESYNOPGMACNOERR)
					TR("ERROR in Phy read\n");	
#endif
			}
		break;

		case IOCTL_WRITE_REGISTER:		//IOCTL for reading IP registers : Read Registers
			if      (req->unit == 0)	// Write Mac Register
				synopGMACWriteReg((u32 *)gmacdev->MacBase,req->addr,req->data);
			else if (req->unit == 1)	// Write DMA Register
			{
				printk("GMAC driver : addr 0x%x, data 0x%x\n", req->addr, req->data);
				synopGMACWriteReg((u32 *)gmacdev->DmaBase,req->addr,req->data);
			}
			else if (req->unit == 2){	// Write Phy Register
				retval = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,(u16)(req->data&0xffff));
#ifdef DEBUG
				if(retval != -ESYNOPGMACNOERR)
					TR("ERROR in Phy read\n");	
#endif
			}			
			break;

		case IOCTL_READ_IPSTRUCT:		//IOCTL for reading GMAC DEVICE IP private structure
		        memcpy(ifr->ifr_data, gmacdev, sizeof(synopGMACdevice));
			break;

		case IOCTL_READ_RXDESC:			//IOCTL for Reading Rx DMA DESCRIPTOR
			memcpy(ifr->ifr_data, gmacdev->RxDesc + ((DmaDesc *) (ifr->ifr_data))->data1, sizeof(DmaDesc) );
			break;

		case IOCTL_READ_TXDESC:			//IOCTL for Reading Tx DMA DESCRIPTOR
			memcpy(ifr->ifr_data, gmacdev->TxDesc + ((DmaDesc *) (ifr->ifr_data))->data1, sizeof(DmaDesc) );
			break;
		case IOCTL_POWER_DOWN:
			if	(req->unit == 1){	//power down the mac
				TR("============I will Power down the MAC now =============\n");
				printk("============I will Power down the MAC now =============\n");
				// If it is already in power down don't power down again
				retval = 0;
				if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) != GmacPmtPowerDown){
				synopGMAC_linux_powerdown_mac(gmacdev);			
				retval = 0;
				}
			}
			if	(req->unit == 2){	//Disable the power down  and wake up the Mac locally
				TR("============I will Power up the MAC now =============\n");
				printk("============I will Power up the MAC now =============\n");
				//If already powered down then only try to wake up
				retval = -1;
				if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) == GmacPmtPowerDown){
				synopGMAC_power_down_disable(gmacdev);
				synopGMAC_linux_powerup_mac(gmacdev);
				retval = 0;
				}
			}
			break;
			
		case IOCTL_CHK_CONN:
			printk("link status %d\n", gmacdev->LinkState);
			memcpy(ifr->ifr_data, &gmacdev->LinkState, sizeof(gmacdev->LinkState));
			break;

		case IOCTL_PHY_LOOPBACK_SETUP:
			if(req == NULL){
				printk("req is NULL\n");
				break;
			}

			if(req->unit == 0){
				synopGMAC_phy_loopback(gmacdev,0);
			}else{
				synopGMAC_phy_loopback(gmacdev,1);
			}
			break;

		case IOCTL_SS_SELF_TEST_MODE_ON_OFF:	
			if(req == NULL){
				printk("req is NULL\n");
				break;
			}
			
			if(req->unit == 1){
				printk("SS Loopback Mode on, please plug the test tool in Ethernet port\n");
				gmacdev->ssLoopbackEnable = 1;
				synopGMAC_frame_filter_disable(gmacdev);
				synopGMAC_rx_tcpip_chksum_drop_disable(gmacdev);
				SS_selftest_frame = dma_alloc_coherent(NULL, MAX_ETHERNET_PAYLOAD + ETHERNET_PACKET_EXTRA, &SS_selftest_frame_phys, GFP_KERNEL);
			}else if (req->unit == 0){
				printk("SS Loopback Mode off\n");
				gmacdev->ssLoopbackEnable = 0;
				synopGMAC_frame_filter_enable(gmacdev);
				synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev);
		                dma_free_coherent(NULL, MAX_ETHERNET_PAYLOAD + ETHERNET_PACKET_EXTRA, SS_selftest_frame, SS_selftest_frame_phys);
			}
			gmacdev->ssLoopbackErrorPkt=0;
			gmacdev->ssLoopbackPassPkt=0;
			break;
			

		case IOCTL_SS_SELF_TEST_SEND:
			gmacdev->ssLoopbackErrorPkt=0;
			gmacdev->ssLoopbackPassPkt=0;
			synopGMAC_SS_self_test_xmit_frames(gmacdev);
			break;

		case IOCTL_SS_SELF_TEST_GET_RESULT:
			if(req == NULL){
				printk("req is NULL\n");
				break;
			}

			if(gmacdev->ssLoopbackPassPkt){
				req->unit =1;
			}else if(gmacdev->ssLoopbackErrorPkt){
				req->unit =2;
			}

			break;

		case IOCTL_SS_SELF_TEST_SET_FRAME:
			if(req == NULL){
				printk("req is NULL\n");
				break;
			}
			if(req->len > MAX_ETHERNET_PAYLOAD){
				retval = -1;
			}
			memset(SS_selftest_frame, 0 ,MAX_ETHERNET_PAYLOAD + ETHERNET_PACKET_EXTRA);
			copy_from_user((void *)SS_selftest_frame, (const void __user *)(req->addr),(unsigned long)req->len);
			gmacdev->ssLoopbackFrameSize = req->len;
			
			break;
				
		case IOCTL_RESET_PHY:
			printk(KERN_CRIT "MAC driver do PHY Reset\n");
			#if (!defined( CONFIG_NVT_INTERNAL_PHY))
			ioremap_OR_value(NVT_PHY_RESET_GPIO_ADDR + 0x8,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
			ioremap_write(NVT_PHY_RESET_GPIO_ADDR,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
			mdelay(1);
			ioremap_write(NVT_PHY_RESET_GPIO_ADDR + 0x4, (u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
			#endif
			break;

		case IOCTL_RESET_MAC:
			printk(KERN_CRIT "MAC driver do MAC Reset\n");
			synopGMAC_reset(gmacdev);
			synopGMAC_linux_close(netdev);
			synopGMAC_linux_open(netdev);
			break;

		case IOCTL_SHOW_DMA_STATUS:
			currdescaddr = synopGMAC_get_dma_current_TX_desc(gmacdev);
			printk("Current description address = 0x%08x\n",currdescaddr);
			currdesc = (DmaDesc *)phys_to_virt(currdescaddr);
			printk("Current description status=0x%08x, length=0x%08x\n",currdesc->status,currdesc->length);
			break;

		case SIOCGMIIPHY:
			mii_data = if_mii(ifr);
			mii_data->phy_id = gmacdev->PhyBase & 0x1f;			
			break;
			
		case SIOCGMIIREG:
			mii_data = if_mii(ifr);
			/*check return value is necessary*/
			retval=synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,mii_data->reg_num,&mii_data->val_out);
			break;
			
		case SIOCSMIIREG:
			if (!capable(CAP_NET_ADMIN))
	     		return -EPERM;	
			mii_data = if_mii(ifr);
			/*check return value is necessary*/
			retval=synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,mii_data->reg_num,mii_data->val_in);
			break;
		default:
			retval = -1;

	}


return retval;
}

/**
 * Function to handle a Tx Hang.
 * This is a software hook (Linux) to handle transmitter hang if any.
 * We get transmitter hang in the device interrupt status, and is handled
 * in ISR. This function is here as a place holder.
 * @param[in] pointer to net_device structure 
 * \return void.
 */
void synopGMAC_linux_tx_timeout(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev = NULL;

	printk(KERN_CRIT "%s called \n",__FUNCTION__);

	adapter = netdev_priv(netdev);		// use netdev_priv() @ 20100520
	if(adapter == NULL){
		TR("Unknown Device\n");
		return;
	}
	
	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure is missing\n");
		return;
	}

	synopGMAC_linux_reset_mac(netdev,gmacdev);
	
	adapter->synopGMACNetStats.tx_errors++;
	return;
}

void synopGMAC_linux_change_rx_flags(struct net_device *netdev, int flags)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev = NULL;

	adapter = netdev_priv(netdev);
	if(adapter == NULL){
		TR("Unknown Device\n");
		return;
	}

	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure is missing\n");
		return;
	}

	if (netdev->flags & IFF_PROMISC) {
		synopGMAC_frame_filter_disable(gmacdev);
		synopGMAC_src_addr_filter_enable(gmacdev);
		synopGMAC_src_addr_filter_inverse(gmacdev);
		printk(KERN_CRIT "[NTKETHMAC]: Promiscuous Mode is enabled.\n");
	}
	else
	{
		synopGMAC_frame_filter_enable(gmacdev);
		synopGMAC_src_addr_filter_disable(gmacdev);
		synopGMAC_src_addr_filter_normal(gmacdev);
		printk(KERN_CRIT "[NTKETHMAC]: Promiscuous Mode is disabled.\n");
	}

	return;
}

// jay hsu : polling function
#ifdef CONFIG_NVT_FASTETH_MAC_NAPI
int synopGMAC_linux_poll(struct napi_struct *napi, int budget)
{	
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	struct net_device *netdev = napi->dev;
	u32 desc_index;
	s8 ret;
	u32 data1;
	u32 data2;
	u32 len;
	s32 skblen;
	u32 status;
	u32 dma_addr1;
	u32 dma_addr2;
#ifdef ENH_DESC_8W
	u32 ext_status;
	u16 time_stamp_higher;
	u32 time_stamp_high;
	u32 time_stamp_low;
#endif
	u32 polling_packets = 0;
	
	struct sk_buff *skb; //This is the pointer to hold the received data
	
	TR("%s\n",__FUNCTION__);	
	
	adapter = netdev_priv(netdev);
	if(adapter == NULL){
		TR("Unknown Device\n");
		return 1;
	}
	
	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure is missing\n");
		return 1;
	}

	/*Handle the Receive Descriptors*/
	do{
#ifdef ENH_DESC_8W
	ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2,&ext_status,&time_stamp_high,&time_stamp_low,&desc_index);
	if(ret == 0){ 
        synopGMAC_TS_read_timestamp_higher_val(gmacdev, &time_stamp_higher);
	TR("S:%08x ES:%08x DA1:%08x d1:%08x DA2:%08x d2:%08x TSH:%08x TSL:%08x TSHW:%08x \n",status,ext_status,dma_addr1, data1,dma_addr2,data2, time_stamp_high,time_stamp_low,time_stamp_higher);
	}
#else
	ret = synopGMAC_get_rx_qptr(gmacdev, &status,&dma_addr1,NULL, &data1,&dma_addr2,NULL,&data2, &desc_index);
	// jay hsu : debug
	TR("##Handle Received## desc_index %d, dma_addr1 0x%x, data1 0x%x\n", desc_index, (u32)dma_addr1, data1);
#endif

		if(ret == 0 && data1 != 0){
			TR("Received Data at Rx Descriptor %d for skb 0x%08x whose status is %08x\n",desc_index,data1,status);

			skb = (struct sk_buff *)data1;
			if(synopGMAC_is_rx_desc_valid(status)){
				len =  synopGMAC_get_rx_desc_frame_length(status) - 4; //Not interested in Ethernet CRC bytes
				skb_put(skb,len);
			#ifdef IPC_OFFLOAD
				// Now lets check for the IPC offloading
				/*  Since we have enabled the checksum offloading in hardware, lets inform the kernel
				    not to perform the checksum computation on the incoming packet. Note that ip header 
  				    checksum will be computed by the kernel immaterial of what we inform. Similary TCP/UDP/ICMP
				    pseudo header checksum will be computed by the stack. What we can inform is not to perform
				    payload checksum. 		
   				    When CHECKSUM_UNNECESSARY is set kernel bypasses the checksum computation.		    
				*/
	
				TR("Checksum Offloading will be done now\n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				
				#ifdef ENH_DESC_8W
				if(synopGMAC_is_ext_status(gmacdev, status)){ // extended status present indicates that the RDES4 need to be probed
					TR("Extended Status present\n");
					if(synopGMAC_ES_is_IP_header_error(gmacdev,ext_status)){       // IP header (IPV4) checksum error
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR("(EXTSTS)Error in IP header error\n");
					skb->ip_summed = CHECKSUM_NONE;     //Let Kernel compute the checkssum
					}	
					if(synopGMAC_ES_is_rx_checksum_bypassed(gmacdev,ext_status)){   // Hardware engine bypassed the checksum computation/checking
					TR("(EXTSTS)Hardware bypassed checksum computation\n");	
					skb->ip_summed = CHECKSUM_NONE;             // Let Kernel compute the checksum
					}
					if(synopGMAC_ES_is_IP_payload_error(gmacdev,ext_status)){       // IP payload checksum is in error (UDP/TCP/ICMP checksum error)
					TR("(EXTSTS) Error in EP payload\n");	
					skb->ip_summed = CHECKSUM_NONE;             // Let Kernel compute the checksum
					}				
				}
				else{ // No extended status. So relevant information is available in the status itself
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxNoChkError ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 4>  \n");
					skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass computing the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR(" Error in 16bit IPV4 Header Checksum <Chk Status = 6>  \n");
					skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass the TCP/UDP checksum computation
					}				
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxLenLT600 ){
					TR("IEEE 802.3 type frame with Length field Lesss than 0x0600 <Chk Status = 0> \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrPayLoadChkBypass ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 1>\n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxChkBypass ){
					TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 3>  \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxPayLoadChkError ){
					TR(" TCP/UDP payload checksum Error <Chk Status = 5>  \n");
					skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
					}
					if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
					//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
					TR(" Both IP header and Payload Checksum Error <Chk Status = 7>  \n");
					skb->ip_summed = CHECKSUM_NONE;	        //Let Kernel compute the Checksum
					}
				}
				#else	
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxNoChkError ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 4>  \n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass computing the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
				//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
				TR(" Error in 16bit IPV4 Header Checksum <Chk Status = 6>  \n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;	//Let Kernel bypass the TCP/UDP checksum computation
				}				
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxLenLT600 ){
				TR("IEEE 802.3 type frame with Length field Lesss than 0x0600 <Chk Status = 0> \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrPayLoadChkBypass ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 1>\n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxChkBypass ){
				TR("Ip header and TCP/UDP payload checksum Bypassed <Chk Status = 3>  \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxPayLoadChkError ){
				TR(" TCP/UDP payload checksum Error <Chk Status = 5>  \n");
				skb->ip_summed = CHECKSUM_NONE;	//Let Kernel compute the Checksum
				}
				if(synopGMAC_is_rx_checksum_error(gmacdev, status) == RxIpHdrChkError ){
				//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
				TR(" Both IP header and Payload Checksum Error <Chk Status = 7>  \n");
				skb->ip_summed = CHECKSUM_NONE;	        //Let Kernel compute the Checksum
				}
				
				#endif
			#endif //IPC_OFFLOAD	
				skb->dev = netdev;
				skb->protocol = eth_type_trans(skb, netdev);
				netif_receive_skb(skb);				

				netdev->last_rx = jiffies;
				adapter->synopGMACNetStats.rx_packets++;
				adapter->synopGMACNetStats.rx_bytes += len;		
				
				polling_packets++;		// jay hsu : increase packet counters
			}
			else{
				/*Now the present skb should be set free*/
				dev_kfree_skb_any(skb);
				printk(KERN_CRIT "status : %08x\n",status);
				adapter->synopGMACNetStats.rx_errors++;
				adapter->synopGMACNetStats.collisions       += synopGMAC_is_rx_frame_collision(status);
				adapter->synopGMACNetStats.rx_crc_errors    += synopGMAC_is_rx_crc(status);
				adapter->synopGMACNetStats.rx_frame_errors  += synopGMAC_is_frame_dribbling_errors(status);
				adapter->synopGMACNetStats.rx_length_errors += synopGMAC_is_rx_frame_length_errors(status);
			}
			
			//Now lets allocate the skb for the emptied descriptor
			skb = dev_alloc_skb(netdev->mtu + ETHERNET_PACKET_EXTRA);
			if(skb == NULL){
				TR("SKB memory allocation failed \n");
				adapter->synopGMACNetStats.rx_dropped++;
				return 1;
			}						
			
			skblen = skb_tailroom(skb);
			if(skblen > 0){
				ret = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0, &desc_index);
			}else{
				ret = -1;
			}
			if(ret < 0){
				TR("Cannot set Rx Descriptor for skb %08x\n",(u32)skb);
				printk("Cannot set Rx Descriptor for skb %08x\n",(u32)skb);	// jay hsu : debug test it
				dev_kfree_skb_any(skb);
			}
		}
	}while(ret == 0 && ( polling_packets < (budget-1) ));
	
	napi_complete(napi);	
	atomic_set(&napi_poll, 0);
	synopGMAC_enable_interrupt(gmacdev, synopGMAC_get_interrupt_mask(gmacdev)|DmaIntRxNormMask);

		
	return polling_packets;
}
#endif

static struct of_device_id of_platform_synopGMAC_table[];
static int synopGMAC_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;

	printk(KERN_CRIT "[NTKETHMAC]: %s.\n", __FUNCTION__);

	match = of_match_device(of_platform_synopGMAC_table, &pdev->dev);
	if (!match)
		return -EINVAL;

	return 0;
}

static int synopGMAC_suspend(struct device *pdev)
{
	struct net_device *netdev = synopGMACadapter->synopGMACnetdev;
        synopGMACPciNetworkAdapter *adapter;
        synopGMACdevice * gmacdev;

	printk(KERN_CRIT "%s: START\n",__FUNCTION__);

	if (netdev == NULL) {
		printk("%s: OOPS net dev is null\n",__FUNCTION__);
		return 0;
	}

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);	// use netdev_priv() @ 20100520
	if(adapter == NULL){
		printk("%s: OOPS adapter is null\n",__FUNCTION__);
		return 0;
	}

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(gmacdev == NULL){
		printk("%s: OOPS gmacdev is null\n",__FUNCTION__);
		return 0;
	}

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
	
	#ifdef USE_DEVICE_TREE
		disable_irq(gmacdev->IRQNum);
	#else	
		disable_irq(GMAC_IRQ);
	#endif

		flush_workqueue(plugDetect_Wq);

	
		del_timer(&NVT_Phy_InitTimer);

		del_timer(&synopGMAC_cable_unplug_timer);

		/*Disable all the interrupts*/
		synopGMAC_disable_interrupt_all(gmacdev);

		/*Disable the reception*/	
		synopGMAC_disable_dma_rx(gmacdev);
	        synopGMAC_take_desc_ownership_rx(gmacdev);

		/*Disable the transmission*/
		synopGMAC_disable_dma_tx(gmacdev);
	        synopGMAC_take_desc_ownership_tx(gmacdev);

		/*Free the TX Rx Descriptor contents*/
		synopGMAC_giveup_rx_desc_queue(gmacdev, NULL, RINGMODE);
		synopGMAC_giveup_tx_desc_queue(gmacdev, NULL, RINGMODE);

		/* Power off MAC */
		synopGMAC_power_down_enable(gmacdev);

		/* Set AXI and AHB RESET of MAC */
		ioremap_OR_value(MAC_AXI_CLK_RESET,0x20000000);
	}
	printk(KERN_CRIT "%s: END\n",__FUNCTION__);
	return 0;
}

static int synopGMAC_resume(struct device *pdev)
{
	s8 status;
	u32 desc_index;
	s32 skblen;
	
#ifdef USE_DEVICE_TREE
	#ifndef CONFIG_NVT_INTERNAL_PHY
	u32 sel_RMII_reg_val;
	#endif
#endif
	struct sk_buff *skb;
        synopGMACPciNetworkAdapter *adapter;
        synopGMACdevice * gmacdev;
	struct net_device *netdev = synopGMACadapter->synopGMACnetdev;

	if (netdev == NULL) {
		printk(KERN_CRIT "%s: OOPS net dev is null\n",__FUNCTION__);
		return 0;
	}

	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);	// use netdev_priv() @ 2010520
	if(adapter == NULL){
		printk(KERN_CRIT "%s:OOPS adapter is null\n",__FUNCTION__);
		return 0;
	}
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;
	if(gmacdev == NULL){
		printk(KERN_CRIT "%s: OOPS gmacdev is null\n",__FUNCTION__);
		return 0;
	}

	if (netif_running(netdev)) {
		SysPlat_Init_Ethernet_Phy(gmacdev, 0);


		synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, Mac_Addr);
		synopGMAC_set_mac_addr(gmacdev,GmacAddr1High,GmacAddr1Low, Mac_Addr);


		synopGMAC_set_mdc_clk_div(gmacdev,g_MDC_CLOCK);

		synopGMAC_setup_tx_desc_queue(gmacdev,NULL,TRANSMIT_DESC_SIZE, RINGMODE);
		synopGMAC_init_tx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr

		synopGMAC_setup_rx_desc_queue(gmacdev,NULL,RECEIVE_DESC_SIZE, RINGMODE);
		synopGMAC_init_rx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr

		synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength8 | DmaDescriptorSkip6);	// jay hsu : skip 6x8 bytes to get next descriptors
		synopGMAC_dma_control_init(gmacdev,DmaStoreAndForward |DmaRxThreshCtrl128);

		if (netdev->flags & IFF_PROMISC) {
			synopGMAC_mac_init(gmacdev, 1);
		}else{
			synopGMAC_mac_init(gmacdev, 0);
		}

		synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation
	
		do{
			skb = alloc_skb(netdev->mtu + ETHERNET_HEADER + ETHERNET_CRC, GFP_ATOMIC);
			if(skb == NULL){
				TR0("ERROR in skb buffer allocation\n");
				break;
			}

			skblen = skb_tailroom(skb);
			if(skblen > 0){
				status = synopGMAC_set_rx_qptr(gmacdev,virt_to_phys(skb->data), (u32)skblen, (u32)skb,0,0,0,&desc_index);
			}else{
				status = -1;
			}
			if(status < 0)
				dev_kfree_skb_any(skb);
		}while(status == 0);

#ifndef CONFIG_NVT_INTERNAL_PHY

#ifdef USE_DEVICE_TREE
		sel_RMII_reg_val = readl(gmacdev->RmiiModeCtrlReg);
		sel_RMII_reg_val  |= 0x80000000;
		writel(sel_RMII_reg_val, gmacdev->RmiiModeCtrlReg);
#else //not USE_DEVICE_TREE
		ioremap_OR_value(MAC_RMII_MII_SEL_ADDR, 0x80000000);
	
		/* External ethernet phy reset */
		ioremap_OR_value(NVT_PHY_RESET_GPIO_ADDR + 0x8,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
		ioremap_write(NVT_PHY_RESET_GPIO_ADDR,(u32)(1UL << NVT_PHY_RESET_GPIO_BIT));
		mdelay(1);
		ioremap_write(NVT_PHY_RESET_GPIO_ADDR + 0x4, (u32)(1UL << NVT_PHY_RESET_GPIO_BIT));

#endif //USE_DEVICE_TREE
#endif //CONFIG_NVT_INTERNAL_PHY
		gmacdev->LinkState = 0;
		do_suspend = 1;


#ifdef CONFIG_NVT_RTL_PHY_SUPPORT
		if(gmacdev->PHY_ID == RTL8201F_PHY_ID){
			synopGMAC_RTL8201F_set_phy_TXC_direction(gmacdev, 1);
			synopGMAC_RTL8201F_set_phy_adjust_RMII_RX_timing(gmacdev, 0xD);
		}
#endif
		synopGMAC_clear_interrupt(gmacdev);
		/*
		Disable the interrupts generated by MMC and IPC counters.
		If these are not disabled ISR should be modified accordingly to handle these interrupts.
		*/	
		synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
		synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
		synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);


		/* Power up MAC */
		synopGMAC_power_down_disable(gmacdev);

		netif_device_attach(netdev);
	
	#ifdef USE_DEVICE_TREE
		enable_irq(gmacdev->IRQNum);
	#else	
		enable_irq(GMAC_IRQ);
	#endif

		init_timer(&synopGMAC_cable_unplug_timer);
		synopGMAC_cable_unplug_timer.function = (void *)synopGMAC_linux_cable_unplug_function;
		synopGMAC_cable_unplug_timer.data = (u32) adapter;
		synopGMAC_cable_unplug_timer.expires = jiffies + (HZ/2);
		add_timer(&synopGMAC_cable_unplug_timer);
	}

	printk(KERN_CRIT "Ethernet resume complete....\n");
	return 0;
}

static struct of_device_id of_platform_synopGMAC_table[] = {
	{ .compatible = "nvt,synopsys-mac" },
	{},
};

MODULE_DEVICE_TABLE(of, of_platform_synopGMAC_table);

static const struct dev_pm_ops synopGMAC_platform_pm_ops = {
        .suspend        = synopGMAC_suspend,
        .resume         = synopGMAC_resume,
};

static struct platform_driver synopGMAC_plat_driver = {
	.driver = {
		.name = "NT72658-synopGMAC",
		.owner = THIS_MODULE,
		.pm = &synopGMAC_platform_pm_ops,
		.bus = &platform_bus_type,
		.of_match_table = of_match_ptr(of_platform_synopGMAC_table),
	},
	.probe   = synopGMAC_probe,
};

// jay hsu : add netdev_ops
static const struct net_device_ops synopGMAC_Netdev_Ops = {
	.ndo_open				= synopGMAC_linux_open,
	.ndo_stop				= synopGMAC_linux_close,
	.ndo_start_xmit			= synopGMAC_linux_xmit_frames,
	.ndo_get_stats			= synopGMAC_linux_get_stats,
	.ndo_set_mac_address	= synopGMAC_linux_set_mac_address,
	.ndo_change_mtu			= synopGMAC_linux_change_mtu,
	.ndo_do_ioctl			= synopGMAC_linux_do_ioctl,
	.ndo_tx_timeout			= synopGMAC_linux_tx_timeout,
	.ndo_change_rx_flags	= synopGMAC_linux_change_rx_flags,
};

/**
 * Function to initialize the Linux network interface.
 * 
 * Linux dependent Network interface is setup here. This provides 
 * an example to handle the network dependent functionality.
 *
 * \return Returns 0 on success and Error code on failure.
 */
s32 __init synopGMAC_init_network_interface(void)
{
	s32 err;
	struct net_device *netdev;

	TR("Now Going to Call register_netdev to register the network interface for GMAC core\n");
	/*
	Lets allocate and set up an ethernet device, it takes the sizeof the private structure. This is mandatory as a 32 byte 
	allignment is required for the private data structure.
	*/
	netdev = alloc_etherdev(sizeof(synopGMACPciNetworkAdapter));
	if(!netdev){
	err = -ESYNOPGMACNOMEM;
	goto err_alloc_etherdev;
	}
	
	synopGMACadapter = netdev_priv(netdev);
	synopGMACadapter->synopGMACnetdev = netdev;
	synopGMACadapter->synopGMACdev    = NULL;
	
	/*Allocate Memory for the the GMACip structure*/
	synopGMACadapter->synopGMACdev = (synopGMACdevice *) plat_alloc_memory(sizeof (synopGMACdevice));
	if(!synopGMACadapter->synopGMACdev){
	TR0("Error in Memory Allocataion #1\n");
	}
	
	TR("## memory allocation at 0x%x\n", (u32)synopGMACadapter->synopGMACdev);
	
	/*Attach the device to MAC struct This will configure all the required base addresses
	  such as Mac base, configuration base, phy base address(out of 32 possible phys )*/
#ifdef USE_DEVICE_TREE
	synopGMAC_attach(synopGMACadapter->synopGMACdev,0 ,0, DEFAULT_PHY_BASE);
#else
	synopGMAC_attach(synopGMACadapter->synopGMACdev, SYNOP_GMAC_BASE_ADDR , SYNOP_DMA_BASE_ADDR, DEFAULT_PHY_BASE);
#endif

	// Init lock for read/write PHY
	sema_init(&sem_phy_rw, 1);

#ifdef CONFIG_NVT_INTERNAL_PHY
	if(nvt_internal_PHY_ioremap_all()){
		printk("[NTKETHMAC] ioremap PHY address fail\n");
	}

	#if DEBUG_READ_PHY_REG
	internal_PHY_OFFSET_0120 = ioremap(0xFD130120, 0x4);
	if(internal_PHY_OFFSET_0120 == NULL){
		return -ESYNOPGMACNOMEM;
	}

	internal_PHY_OFFSET_012C = ioremap(0xFD13012C, 0x4);
	if(internal_PHY_OFFSET_012C == NULL){
		return -ESYNOPGMACNOMEM;
	}

	internal_PHY_OFFSET_0130 = ioremap(0xFD130130, 0x4);
	if(internal_PHY_OFFSET_0130 == NULL){
		return -ESYNOPGMACNOMEM;
	}
	#endif

#else
	external_PHY_reset_dir = ioremap(NVT_PHY_RESET_GPIO_ADDR + 0x8, 0x4);
	if(external_PHY_reset_dir == NULL){
		return -ESYNOPGMACNOMEM;
	}

	external_PHY_reset_pull_low = ioremap(NVT_PHY_RESET_GPIO_ADDR, 0x4);
	if(external_PHY_reset_pull_low == NULL){
		return -ESYNOPGMACNOMEM;
	}

	external_PHY_reset_pull_high = ioremap(NVT_PHY_RESET_GPIO_ADDR + 0x4, 0x4);
	if(external_PHY_reset_pull_high == NULL){
		return -ESYNOPGMACNOMEM;
	}
#endif

	plugDetect_Wq = create_singlethread_workqueue("Eth_plugDetect");

	// jay hsu : initialize the ethernet phy
	SysPlat_Init_Ethernet_Phy(synopGMACadapter->synopGMACdev, 1);
	
	// Jay Hsu : to initialize the mac address
	SysPlat_Init_Mac_Addr();
	
	synopGMAC_reset(synopGMACadapter->synopGMACdev);

	
	// jay hsu : refer e100.c net driver
	netdev->netdev_ops = &synopGMAC_Netdev_Ops;
	netdev->watchdog_timeo = 5 * HZ;
	
	// jay hsu @ 20100727 : init tx spin_lock
	spin_lock_init(&synopGMACadapter->synopGMACdev->tx_lock);
 
	// jay hsu : set MDC clock by ahb auto-detection function
	//if ( KER_CLK_GetClockRate(EN_KER_CLK_AHB_BUS) > 100 )
	// jay hsu : no ntclk.ko support. Add later. @ 20100520
#if 0   // Temportly fix for FPGA verification
	if ( (hclk/1000/1000) >= 90 )
	{
		printk("[NTKETHMAC] : Set MDC clock 100~150MHz\n");
		g_MDC_CLOCK = GmiiCsrClk1;
	}
	else
	{
		printk("[NTKETHMAC] : Set MDC clock 60~100MHz\n");
		g_MDC_CLOCK = GmiiCsrClk0;
	}
#else	
	g_MDC_CLOCK = GmiiCsrClk1;
#endif
	err = platform_driver_register(&synopGMAC_plat_driver);
	if(err){
		printk("[NTKETHMAC] Register platform driver fail\n");
		return err;
	}
	
	/*Now start the network interface*/
	printk(KERN_CRIT "[NTKETHMAC] : Now Registering the netdevice\n");
	TR("Now Registering the netdevice\n");
	if((err = register_netdev(netdev)) != 0) {
		TR0("Error in Registering netdevice\n");
		return err;
	}  
	
#ifdef CONFIG_NVT_FASTETH_MAC_NAPI	
	printk("[NTKETHMAC] : Init with NAPI support\n");
	netif_napi_add(netdev, &nvt_napi, synopGMAC_linux_poll, NVT_NAPI_WEIGHT);
	
#endif	

	/* Set up MAC address */
	synopGMAC_set_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr0High,GmacAddr0Low, Mac_Addr);
	synopGMAC_set_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr1High,GmacAddr1Low, Mac_Addr);

	synopGMAC_get_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr);

 	return 0;
err_alloc_etherdev:
	TR0("Problem in alloc_etherdev()..Take Necessary action\n");
	return err;
}


/**
 * Function to initialize the Linux network interface.
 * Linux dependent Network interface is setup here. This provides 
 * an example to handle the network dependent functionality.
 * \return Returns 0 on success and Error code on failure.
 */
void __exit synopGMAC_exit_network_interface(void)
{
	TR0("Now Calling network_unregister\n");
	unregister_netdev(synopGMACadapter->synopGMACnetdev);	
	
	platform_driver_unregister(&synopGMAC_plat_driver);

#ifdef CONFIG_NVT_INTERNAL_PHY
	nvt_internal_PHY_iounmap_all();
#else
	iounmap(external_PHY_reset_dir);
	iounmap(external_PHY_reset_pull_low);
	iounmap(external_PHY_reset_pull_high);
#endif

	flush_workqueue(plugDetect_Wq);
	destroy_workqueue(plugDetect_Wq);
}


// Jay Hsu : Finally we need to initialize the value of mac address from flash or eeprom.
void SysPlat_Init_Mac_Addr(void)
{
#define DEFAULT_MAC_ADDRESS {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

	u8 default_mac_addr[6] = DEFAULT_MAC_ADDRESS;

	memcpy(Mac_Addr, default_mac_addr, sizeof(Mac_Addr));
	
	return;
}

