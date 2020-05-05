/*
 * xhci-mstar.c - xHCI host controller driver platform Bus Glue.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "xhci.h"
#include "xhci-mstar.h"

static void xhci_plat_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_BROKEN_MSI;
	xhci->quirks |= XHCI_RESET_ON_RESUME;
	xhci->quirks |= XHCI_SPURIOUS_SUCCESS;
	// xhci->quirks |= XHCI_TRUST_TX_LENGTH;
	xhci->hci_version = 0x96;  //modified for real version. 
}

/* called during probe() after chip reset completes */
static int xhci_plat_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, xhci_plat_quirks);
}

static const struct hc_driver mstar_plat_xhci_driver = {
	.description =		"mstar-xhci-hcd",
	.product_desc =		"Mstar xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		xhci_plat_setup,
	.start =		xhci_run,
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/* Root hub support */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,
};


static void XHCI_enable_PPC(unsigned int U3TOP_base)
{
	u16 addr_w, bit_num;
	u32 addr;
	u8  value, low_active;

	addr_w = readw((void*)(U3TOP_base+0xFC*2));
	addr = (u32)addr_w << 8;
	addr_w = readw((void*)(U3TOP_base+0xFE*2));
	addr |= addr_w & 0xFF;
	bit_num = (addr_w >> 8) & 0x7;
	low_active = (u8)((addr_w >> 8) & 0x8);
	
	if (addr)
	{
		printk("XHCI_enable_PPC: Turn on USB3.0 port power \n");
		printk("Addr: 0x%x bit_num: %d low_active:%d\n", addr, bit_num, low_active);

		value = (u8)(1 << bit_num);

		if (low_active)
		{
			if (addr & 0x1)
				writeb(readb((void*)(_MSTAR_PM_BASE+addr*2-1)) & (u8)(~value), (void*)(_MSTAR_PM_BASE+addr*2-1)); 
			else
				writeb(readb((void*)(_MSTAR_PM_BASE+addr*2)) & (u8)(~value), (void*)(_MSTAR_PM_BASE+addr*2)); 
		}
		else
		{
			if (addr & 0x1)
				writeb(readb((void*)(_MSTAR_PM_BASE+addr*2-1)) | value, (void*)(_MSTAR_PM_BASE+addr*2-1)); 
			else
				writeb(readb((void*)(_MSTAR_PM_BASE+addr*2)) | value, (void*)(_MSTAR_PM_BASE+addr*2)); 
		}
	}

}

#if !defined(XHCI_PHY_MT28)
static void Mstar_U3phy_init(unsigned int U3PHY_D_base,unsigned int U3PHY_A_base)
{

	//U3phy initial sequence 
	writew(0x0,    (void*) (U3PHY_A_base)); 		 // power on rx atop 
	writew(0x0,    (void*) (U3PHY_A_base+0x2*2));	 // power on tx atop
	writew(0x0910, (void*) (U3PHY_D_base+0x4*2));  
	writew(0x0,    (void*) (U3PHY_A_base+0x3A*2)); 
	writew(0x0160, (void*) (U3PHY_D_base+0x18*2)); 
	writew(0x0,    (void*) (U3PHY_D_base+0x20*2));	 // power on u3_phy clockgen 
	writew(0x0,    (void*) (U3PHY_D_base+0x22*2));	 // power on u3_phy clockgen 

	writew(0x013F, (void*) (U3PHY_D_base+0x4A*2)); 
	writew(0x1010, (void*) (U3PHY_D_base+0x4C*2)); 

	writew(0x0,    (void*) (U3PHY_A_base+0x3A*2));	 // override PD control
	writeb(0x1C,   (void*) (U3PHY_A_base+0xCD*2-1)); // reg_test_usb3aeq_acc;  long EQ converge 
	writeb(0x40,   (void*) (U3PHY_A_base+0xC9*2-1)); // reg_gcr_usb3aeq_threshold_abs
	writeb(0x10,   (void*) (U3PHY_A_base+0xE5*2-1)); // [4]: AEQ select PD no-delay and 2elay path, 0: delay, 1: no-
	writeb(0x11,   (void*) (U3PHY_A_base+0xC6*2));	 // analog symbol lock and EQ converage step 
	writeb(0x02,   (void*) (U3PHY_D_base+0xA0*2));	 // [1] aeq mode

	writeb(0x07,   (void*) (U3PHY_A_base+0xB0*2));	 // reg_gcr_usb3rx_eq_str_ov_value

	#if (ENABLE_XHCI_SSC)  
		writew(0x04D8,	(void*) (U3PHY_D_base+0xC6*2));  //reg_tx_synth_span
		writew(0x0003,	(void*) (U3PHY_D_base+0xC4*2));  //reg_tx_synth_step
		writew(0x9375,	(void*) (U3PHY_D_base+0xC0*2));  //reg_tx_synth_set
		writeb(0x18,	(void*) (U3PHY_D_base+0xC2*2));  //reg_tx_synth_set
	#endif	

	////Set Tolerance  //only for Agate_U01
	/// writew(0x0103, (void*) (U3PHY_D_base+0x44*2)); 

	// Comma
	// writeb(0x84,   (void*) (U3PHY_A_base+0xCD*2-1)); // reg_test_aeq_acc, 8bit

	// RX phase control
	writew(0x0100, (void*)(U3PHY_A_base+0x90*2));	
	writew(0x0302, (void*)(U3PHY_A_base+0x92*2));	
	writew(0x0504, (void*)(U3PHY_A_base+0x94*2));	
	writew(0x0706, (void*)(U3PHY_A_base+0x96*2));	
	writew(0x1708, (void*)(U3PHY_A_base+0x98*2));	
	writew(0x1516, (void*)(U3PHY_A_base+0x9A*2));	
	writew(0x1314, (void*)(U3PHY_A_base+0x9C*2));	
	writew(0x1112, (void*)(U3PHY_A_base+0x9E*2));	
	writew(0x3000, (void*)(U3PHY_D_base+0xA8*2)); 
	writew(0x7380, (void*)(U3PHY_A_base+0x40*2));	

	#if 0
	//#if (XHCI_ENABLE_DEQ)	
		DEQ_init(U3PHY_D_base, U3PHY_A_base);
	#endif

	//XHCI_TX_SWING
	writeb(0x3F, (void*)(U3PHY_A_base+0x60*2)); 
	writeb(0x39, (void*)(U3PHY_A_base+0x62*2)); 

}
#endif

#if defined(XHCI_PHY_MT28)
static void Mstar_U3phy_MT28_init(unsigned int U3PHY_D_M0_base, unsigned int U3PHY_D_M1_base, 
	                       unsigned int U3PHY_A_M0_base, unsigned int U3PHY_A_M1_base)
{
	u16 value;

	//DA_SSUSB_TX_BIASI_B
	value = (u16)((readw((void*)(U3PHY_D_M1_base+0xA*2)) & (u16)(~0x0E00)) | 0x2000);
	writew(value,  (void*)(U3PHY_D_M1_base+0xA*2));
	//DA_SSUSB_idem_6db_b_olt
	value = (readw((void*)(U3PHY_D_M1_base+0x8*2)) & (u16)(~0x003F));
	value |= (0x100|0x18);	
	writew(value,  (void*)(U3PHY_D_M1_base+0x8*2));
	//DA_SSUSB_IDRV_6DB_B_olt
	value = (readw((void*)(U3PHY_D_M1_base+0x6*2)) & (u16)(~0x3F00));
	value |= (0x4000|0x2400);
	writew(value,  (void*)(U3PHY_D_M1_base+0x6*2));	
	//DA_SSUSB_IDEM_3P5db_B_olt	
	value = (readw((void*)(U3PHY_D_M1_base+0x6*2)) & (u16)(~0x003F));
	value |= (0x40|0x10);
	writew(value,  (void*)(U3PHY_D_M1_base+0x6*2));
	//DA_SSUSB_IDRV_3P5db_b_olt
	value = (readw((void*)(U3PHY_D_M1_base+0x4*2)) & (u16)(~0x3F00));
	value |= (0x4000|0x2800);
	writew(value,  (void*)(U3PHY_D_M1_base+0x4*2));
	//DA_SSUSB_IDRV_0DB_b_olt
	value = (readw((void*)(U3PHY_D_M1_base+0x4*2)) & (u16)(~0x003F));
	value |= (0x40|0x30);
	writew(value,  (void*)(U3PHY_D_M1_base+0x4*2));

	//reg_ssusb_sigdet
	value = (readw((void*)(U3PHY_D_M0_base+0x8E*2)) & (u16)(~0x7F00));
	value |= (0x500);
	writew(value,  (void*)(U3PHY_D_M0_base+0x8E*2));

}
#endif

static void Mstar_xhc_init(unsigned int UTMI_base, unsigned int XHCI_base, 
	unsigned int U3TOP_base, unsigned int U3BC_base, unsigned int flag)
{
	if ((flag & EHCFLAG_RESUME) == 0)
	printk("Mstar_xhc_init version:%s\n", XHCI_MSTAR_VERSION);

	if (0 == readw((void*)(UTMI_base+0x20*2)))
	{
		printk("utmi clk enable\n");
	    writew(0x8051, (void*) (UTMI_base+0x20*2)); 
	    writew(0x2088, (void*) (UTMI_base+0x22*2)); 
	    writew(0x0004, (void*) (UTMI_base+0x2*2)); 
	    writew(0x6BC3, (void*) (UTMI_base)); 
	    mdelay(1);
	    writew(0x69C3, (void*) (UTMI_base)); 
	    mdelay(1);
	    writew(0x0001, (void*) (UTMI_base)); 
	    mdelay(1);
	}
	
    writeb(0x07, (void*) (UTMI_base+0x8*2));   //default value 0x7; don't change it. 

    if (flag & EHCFLAG_TESTPKG)
    {
	    writew(0x2084, (void*) (UTMI_base+0x2*2));
	    writew(0x8051, (void*) (UTMI_base+0x20*2));
    }

	#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
		/*
		 * patch for DM always keep high issue
		 * init overwrite register
		 */
		writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT3), (void*) (UTMI_base+0x0*2)); //DP_PUEN = 0
		writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT4), (void*) (UTMI_base+0x0*2)); //DM_PUEN = 0

		writeb(readb((void*)(UTMI_base+0x0*2)) & (u8)(~BIT5), (void*) (UTMI_base+0x0*2)); //R_PUMODE = 0

		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT6, (void*) (UTMI_base+0x0*2)); //R_DP_PDEN = 1
		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT7, (void*) (UTMI_base+0x0*2)); //R_DM_PDEN = 1

		writeb(readb((void*)(UTMI_base+0x10*2)) | BIT6, (void*) (UTMI_base+0x10*2)); //hs_txser_en_cb = 1
		writeb(readb((void*)(UTMI_base+0x10*2)) & (u8)(~BIT7), (void*) (UTMI_base+0x10*2)); //hs_se0_cb = 0

		/* turn on overwrite mode */
		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT1, (void*) (UTMI_base+0x0*2)); //tern_ov = 1
	#endif	

#if 0
	writeb(readb((void*)(UTMI_base+0x09*2-1)) & (u8)(~0x08), (void*) (UTMI_base+0x09*2-1)); // Disable force_pll_on
	writeb(readb((void*)(UTMI_base+0x08*2)) & (u8)(~0x80), (void*) (UTMI_base+0x08*2)); // Enable band-gap current
	writeb(0xC3, (void*)(UTMI_base)); // reg_pdn: bit<15>, bit <2> ref_pdn
	mdelay(1);	// delay 1ms

	writeb(0x69, (void*) (UTMI_base+0x01*2-1)); // Turn on UPLL, reg_pdn: bit<9>
	mdelay(2);	// delay 2ms

	writeb(0x01, (void*) (UTMI_base)); // Turn all (including hs_current) use override mode
	writeb(0, (void*) (UTMI_base+0x01*2-1)); // Turn on UPLL, reg_pdn: bit<9>
#endif

	writeb(readb((void*)(UTMI_base+0x3C*2)) | 0x01, (void*) (UTMI_base+0x3C*2)); // set CA_START as 1
	mdelay(1);

	writeb(readb((void*)(UTMI_base+0x3C*2)) & (u8)(~0x01), (void*) (UTMI_base+0x3C*2)); // release CA_START

	while ((readb((void*)(UTMI_base+0x3C*2)) & 0x02) == 0);	// polling bit <1> (CA_END)

    if (flag & EHCFLAG_DPDM_SWAP)
    	writeb(readb((void*)(UTMI_base+0x0b*2-1)) |0x20, (void*) (UTMI_base+0x0b*2-1)); // dp dm swap

	writeb((u8)((readb((void*)(UTMI_base+0x06*2)) & 0x9F) | 0x40), (void*) (UTMI_base+0x06*2)); //reg_tx_force_hs_current_enable

	writeb(readb((void*)(UTMI_base+0x03*2-1)) | 0x28, (void*) (UTMI_base+0x03*2-1)); //Disconnect window select
	writeb(readb((void*)(UTMI_base+0x03*2-1)) & 0xef, (void*) (UTMI_base+0x03*2-1)); //Disconnect window select

	writeb(readb((void*)(UTMI_base+0x07*2-1)) & 0xfd, (void*) (UTMI_base+0x07*2-1)); //Disable improved CDR
	writeb(readb((void*)(UTMI_base+0x09*2-1)) |0x81, (void*) (UTMI_base+0x09*2-1)); // UTMI RX anti-dead-loc, ISI effect improvement
	writeb(readb((void*)(UTMI_base+0x0b*2-1)) |0x80, (void*) (UTMI_base+0x0b*2-1)); // TX timing select latch path
	writeb(readb((void*)(UTMI_base+0x15*2-1)) |0x20, (void*) (UTMI_base+0x15*2-1)); // Chirp signal source select

	//Enable XHCI keep-alive
	writeb(readb((void*)(UTMI_base+0x3F*2-1)) |0x80, (void*) (UTMI_base+0x3F*2-1)); // Enable XHCI pream function

	// for 240's phase as 120's clock
	writeb(readb((void*)(UTMI_base+0x08*2)) | 0x18, (void*) (UTMI_base+0x08*2)); // for 240's phase as 120's clock

	// change to 55 timing; for all chips.
	writeb(readb((void*)(UTMI_base+0x15*2-1)) |0x40, (void*) (UTMI_base+0x15*2-1)); // change to 55 timing

    // for CLK 120 override enable; for xHCI on all chips
	writeb(readb((void*)(UTMI_base+0x09*2-1)) |0x04, (void*) (UTMI_base+0x09*2-1)); // for CLK 120 override enable

	if (0 != readb((void *)(PM_TOP_base+0x2*2))) // chip revision    
		writeb(0x60, (void*)(UTMI_base+0x2a*2));

	writeb(XHC_UTMI_EYE_2C, (void*) (UTMI_base+0x2c*2));
	writeb(XHC_UTMI_EYE_2D, (void*) (UTMI_base+0x2d*2-1));
	writeb(XHC_UTMI_EYE_2E, (void*) (UTMI_base+0x2e*2));
	writeb(XHC_UTMI_EYE_2F, (void*) (UTMI_base+0x2f*2-1));

	#if defined(ENABLE_LS_CROSS_POINT_ECO)
		writeb(readb((void*)(UTMI_base+0x04*2)) | 0x40, (void*) (UTMI_base+0x04*2));  //enable deglitch SE0¡¨(low-speed cross point)
	#endif

	#if defined(ENABLE_TX_RX_RESET_CLK_GATING_ECO)
		writeb(readb((void*)(UTMI_base+0x04*2)) | 0x20, (void*) (UTMI_base+0x04*2)); //enable hw auto deassert sw reset(tx/rx reset)
	#endif

	#if defined(ENABLE_KEEPALIVE_ECO)
		writeb(readb((void*)(UTMI_base+0x04*2)) | 0x80, (void*) (UTMI_base+0x04*2));    //enable LS keep alive & preamble
	#endif

	#if defined(ENABLE_HS_DM_KEEP_HIGH_ECO)
		/* Change override to hs_txser_en.  Dm always keep high issue */ 
		writeb(readb((void*)(UTMI_base+0x10*2)) | BIT6, (void*) (UTMI_base+0x10*2));
	#endif

	#if _USB_HS_CUR_DRIVE_DM_ALLWAYS_HIGH_PATCH
		/*
		 * patch for DM always keep high issue
		 * init overwrite register
		 */
		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT6, (void*) (UTMI_base+0x0*2)); //R_DP_PDEN = 1
		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT7, (void*) (UTMI_base+0x0*2)); //R_DM_PDEN = 1

		/* turn on overwrite mode */
		writeb(readb((void*)(UTMI_base+0x0*2)) | BIT1, (void*) (UTMI_base+0x0*2)); //tern_ov = 1
	#endif

	#if _XHCI_HS_SQUELCH_ADJUST_PATCH
	/* squelch level adjust by calibration value */
	if (0 == readb((void *)(PM_TOP_base+0x2*2))) // chip revision        
	{
		unsigned int ca_da_ov, ca_db_ov, ca_tmp;

		ca_tmp = readw((void*)(UTMI_base+0x3c*2));
		ca_da_ov = (((ca_tmp >> 4) & 0x3f) - 5) + 0x40;
		ca_db_ov = (((ca_tmp >> 10) & 0x3f) - 5) + 0x40;
		if ((flag & EHCFLAG_RESUME) == 0)
		printk("[%x]-5 ->(ca_da_ov, ca_db_ov) = (%x,%x)\n", ca_tmp, ca_da_ov, ca_db_ov);
		writeb(ca_da_ov ,(void*)(UTMI_base+0x3B*2-1));
		writeb(ca_db_ov ,(void*)(UTMI_base+0x24*2));
	}
	#endif

    if (flag & EHCFLAG_TESTPKG)
    {
	    writew(0x0600, (void*) (UTMI_base+0x14*2)); 
	    writew(0x0038, (void*) (UTMI_base+0x10*2)); 
	    writew(0x0BFE, (void*) (UTMI_base+0x32*2)); 
    }

	// Init USB3 PHY 
	#if defined(XHCI_PHY_MT28)
		Mstar_U3phy_MT28_init(_MSTAR_U3PHY_DTOP_M0_BASE, _MSTAR_U3PHY_DTOP_M1_BASE,
		                      _MSTAR_U3PHY_ATOP_M0_BASE, _MSTAR_U3PHY_ATOP_M1_BASE);
	#else
		Mstar_U3phy_init(_MSTAR_U3PHY_DTOP_BASE, _MSTAR_U3PHY_ATOP_BASE);
	#endif  

	//First token idle
	writeb(readb((void*)(XHCI_base+0x4308)) | 0x0C, (void*)(XHCI_base+0x4308));  //First token idle (bit2~3 = "11")
	//Inter packet delay
	writeb(readb((void*)(XHCI_base+0x430F)) | 0xC0, (void*)(XHCI_base+0x430F));  //Inter packet delay (bit6~7 = "11")
	//LFPS patch
	writeb(readb((void*)(XHCI_base+0x681A)) | 0x10, (void*)(XHCI_base+0x681A));  //LFPS patch  (bit4 = 1)
	
	//Bus Reset setting => default 50ms
	writeb((u8)((readb((void*)(U3TOP_base+0x24*2)) & 0xF0) | 0x8), (void*)(U3TOP_base+0x24*2));    // [5] = reg_debug_mask to 1'b0

	//check both last_down_z & data count enable
		writeb(readb((void*)(U3TOP_base+0x12*2)) | 0x8, (void*)(U3TOP_base+0x12*2));  //check both last_down_z & data count enable

	#if (XHCI_CHIRP_PATCH)
		if (0 == readb((void *)(PM_TOP_base+0x2*2))) // chip revision   
			writeb(0x0, (void*) (UTMI_base+0x3E*2)); //override value 
		else
			writeb(0x60, (void*) (UTMI_base+0x3E*2)); //override value 
		writeb((u8)((readb((void*)(U3TOP_base+0x24*2)) & 0xF0) | 0x0A), (void*)(U3TOP_base+0x24*2)); // set T1=50, T2=20
		writeb(readb((void*)(UTMI_base+0x3F*2-1)) | 0x1, (void*) (UTMI_base+0x3F*2-1)); //enable the patch 
	#endif 

	#if defined (XHCI_ENABLE_LOOPBACK_ECO)
			writeb(readb((void*)(U3TOP_base+0x20*2))|0x30 , (void*)(U3TOP_base+0x20*2));
		#endif

	//---- Disable BC ----
		writeb(readb((void *)(U3BC_base+(0xc*2))) & (u8)(~0x40), (void *)(U3BC_base+(0xc*2)));  // [6]= reg_into_host_bc_sw_tri
		writeb(readb((void *)(U3BC_base+(0x3*2-1))) & (u8)(~0x40), (void *)(U3BC_base+(0x3*2-1)));  // [6]= reg_host_bc_en
		writeb(readb((void *)(UTMI_base+(0x1*2-1))) & (u8)(~0x40), (void *)(UTMI_base+(0x1*2-1)));  //IREF_PDN=1¡¦b1. (utmi+0x01[6] )
    //------------------
	
	#ifdef XHCI_ENABLE_PPC
		XHCI_enable_PPC(U3TOP_base);
	#endif
	
}


static int xhci_mstar_plat_probe(struct platform_device *pdev)
{
	const struct hc_driver	*driver;
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd;
	int			ret;
	int			irq;	
	unsigned int flag=0;

	if (usb_disabled())
		return -ENODEV;

	//printk("xHCI_%x%04x \n", readb((void*)(_MSTAR_PM_BASE+0x1ECC*2)), readw((void*)(_MSTAR_PM_BASE+0x1ECE*2)));
	printk("Mstar-xhci H.W init\n");
	Mstar_xhc_init(_MSTAR_U3UTMI_BASE, _MSTAR_XHCI_BASE,
	           _MSTAR_U3TOP_BASE, _MSTAR_U3BC_BASE, flag);

	driver = &mstar_plat_xhci_driver;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto put_hcd;
	}

	hcd->regs = (void *)(u32)(hcd->rsrc_start);	        	
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto release_mem_region;
	}

	hcd->xhci_base = _MSTAR_XHCI_BASE;
#if defined(XHCI_PHY_MT28)
	hcd->u3phy_d_m0_base = _MSTAR_U3PHY_DTOP_M0_BASE;
	hcd->u3phy_d_m1_base = _MSTAR_U3PHY_DTOP_M1_BASE;
	hcd->u3phy_a_m0_base = _MSTAR_U3PHY_ATOP_M0_BASE;
	hcd->u3phy_a_m1_base = _MSTAR_U3PHY_ATOP_M1_BASE;
#else	
	hcd->u3phy_d_base = _MSTAR_U3PHY_DTOP_BASE;
	hcd->u3phy_a_base = _MSTAR_U3PHY_ATOP_BASE;
#endif	
	hcd->u3top_base = _MSTAR_U3TOP_BASE;
	hcd->utmi_base = _MSTAR_U3UTMI_BASE;
	hcd->bc_base = _MSTAR_U3BC_BASE;
	hcd->lock_usbreset=__SPIN_LOCK_UNLOCKED(hcd->lock_usbreset);
#if defined(XHCI_RETRY_FOR_BAD_DEVICE)	
	hcd->enum_port_flag = 0;
#endif	

	ret = usb_add_hcd(hcd, (unsigned int)irq, IRQF_DISABLED /* | IRQF_SHARED */);
	if (ret)
		goto unmap_registers;

	/* USB 2.0 roothub is stored in the platform_device now. */
	hcd = dev_get_drvdata(&pdev->dev);
	if (hcd)
		xhci = hcd_to_xhci(hcd);
	else
		return -ENODEV;
	xhci->shared_hcd = usb_create_shared_hcd(driver, &pdev->dev,
			dev_name(&pdev->dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	/*
	 * Set the xHCI pointer before xhci_plat_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	xhci->shared_hcd->xhci_base = _MSTAR_XHCI_BASE;
#if defined(XHCI_PHY_MT28)
	xhci->shared_hcd->u3phy_d_m0_base = _MSTAR_U3PHY_DTOP_M0_BASE;
	xhci->shared_hcd->u3phy_d_m1_base = _MSTAR_U3PHY_DTOP_M1_BASE;
	xhci->shared_hcd->u3phy_a_m0_base = _MSTAR_U3PHY_ATOP_M0_BASE;
	xhci->shared_hcd->u3phy_a_m1_base = _MSTAR_U3PHY_ATOP_M1_BASE;
#else
	xhci->shared_hcd->u3phy_d_base = _MSTAR_U3PHY_DTOP_BASE;
	xhci->shared_hcd->u3phy_a_base = _MSTAR_U3PHY_ATOP_BASE;
#endif
	xhci->shared_hcd->u3top_base = _MSTAR_U3TOP_BASE;
	xhci->shared_hcd->utmi_base = _MSTAR_U3UTMI_BASE;	
	xhci->shared_hcd->bc_base = _MSTAR_U3BC_BASE;	
	xhci->shared_hcd->lock_usbreset=__SPIN_LOCK_UNLOCKED(xhci->shared_hcd->lock_usbreset);
#if defined(XHCI_RETRY_FOR_BAD_DEVICE)	
	xhci->shared_hcd->enum_port_flag = 0;
#endif

	ret = usb_add_hcd(xhci->shared_hcd, (unsigned int)irq, IRQF_DISABLED /* | IRQF_SHARED */);
	if (ret)
		goto put_usb3_hcd;

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

unmap_registers:
	iounmap(hcd->regs);

release_mem_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}


static int xhci_mstar_plat_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci;

	if (hcd)
		xhci = hcd_to_xhci(hcd);
	else
		return -ENODEV;

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	kfree(xhci);

	return 0;
}

#if defined(CONFIG_MSTAR_USB_STR_PATCH) && defined(CONFIG_MSTAR_X14)
static int xhci_hcd_mstar_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;

	printk("xhci_hcd_mstar_drv_suspend \n");

	hcd = platform_get_drvdata(pdev);
	if (hcd)
		xhci = hcd_to_xhci(hcd);
	else
		return -ENODEV;
	
	return xhci_suspend(xhci);
}

static int xhci_hcd_mstar_drv_resume(struct platform_device *pdev)
{
	unsigned int flag=EHCFLAG_RESUME;
	struct xhci_hcd *xhci;
	int	   retval = 0;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

#ifdef CONFIG_MSTAR_DRIVER_DBG
	printk("xhci_hcd_mstar_drv_resume \n");
#endif
	
	Mstar_xhc_init(_MSTAR_U3UTMI_BASE, _MSTAR_XHCI_BASE,
	           _MSTAR_U3TOP_BASE, _MSTAR_U3BC_BASE, flag);    

	if (hcd)
		xhci = hcd_to_xhci(hcd);
	else
		return -ENODEV;
	retval = xhci_resume(xhci, false);
    if (retval) {
        printk(" xhci_resume FAIL : -0x%x !!", -retval); 
        return retval; 
    }        
	//enable_irq(hcd->irq);

	return 0;
}
#endif

static struct platform_driver xhci_mstar_driver = {

	.probe =	xhci_mstar_plat_probe,
	.remove =	xhci_mstar_plat_remove,
#if defined(CONFIG_MSTAR_USB_STR_PATCH) && defined(CONFIG_MSTAR_X14)
	.suspend	= xhci_hcd_mstar_drv_suspend,
	.resume		= xhci_hcd_mstar_drv_resume, 
#endif
    .driver = {
        .name   = "Mstar-xhci-1",
	},
};
MODULE_ALIAS("platform:mstar-xhci-hcd");

int xhci_register_plat(void)
{
	return platform_driver_register(&xhci_mstar_driver);
}

void xhci_unregister_plat(void)
{
	platform_driver_unregister(&xhci_mstar_driver);
}
