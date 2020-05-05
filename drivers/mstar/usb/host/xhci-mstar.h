/*
 * xHCI host controller driver
 *
 * Copyright (C) 2013 MStar Inc.
 *
 */

#ifndef _XHCI_MSTAR_H
#define _XHCI_MSTAR_H

#include "ehci-mstar.h"

#define XHCI_MSTAR_VERSION "20140115"

// ----- Don't modify it !----------
#if defined(CONFIG_ARM) 
#define XHCI_PA_PATCH   1
#else
#define XHCI_PA_PATCH   0
#endif
#define XHCI_FLUSHPIPE_PATCH  1
//------------------------------

#define XHCI_CHIRP_PATCH  1
#define ENABLE_XHCI_SSC   1

//Inter packet delay setting for all 
#define XHCI_IPACKET_DELAY_PATCH

#define XHCI_DISABLE_TESTMODE
#define XHCI_SSDISABLED_PATCH

#define XHCI_ENABLE_PPC

#define XHCI_ENABLE_LOOPBACK_ECO

#define _XHCI_HS_SQUELCH_ADJUST_PATCH   1  //Temporary solution before IC ECO.

#define XHCI_RETRY_FOR_BAD_DEVICE      //For Atheros WiFi dongle not work under full speed.

//--------  U3 PHY IP  -----------

#if defined(CONFIG_MSTAR_X14)
#define XHCI_PHY_MT28	
#endif

//--------------------------------

#if defined(CONFIG_ARM)
#define _MSTAR_PM_BASE         0xFD000000
#else
#define _MSTAR_PM_BASE         0xBF000000
#endif

#if defined(CONFIG_MSTAR_X14)
#define _MSTAR_U3PHY_ATOP_M0_BASE (_MSTAR_USB_BASEADR+(0x22100*2))
#define _MSTAR_U3PHY_ATOP_M1_BASE (_MSTAR_USB_BASEADR+(0x22200*2))
#define _MSTAR_U3PHY_DTOP_M0_BASE (_MSTAR_USB_BASEADR+(0x11C00*2))
#define _MSTAR_U3PHY_DTOP_M1_BASE (_MSTAR_USB_BASEADR+(0x11D00*2))
#define _MSTAR_U3UTMI_BASE     (_MSTAR_USB_BASEADR+(0x22300*2))
#define _MSTAR_U3TOP_BASE      (_MSTAR_USB_BASEADR+(0x22500*2))
#define _MSTAR_XHCI_BASE       (_MSTAR_USB_BASEADR+(0x90000*2))
#define _MSTAR_U3BC_BASE       (_MSTAR_USB_BASEADR+(0x23680*2))
#endif


//------ UTMI eye diagram parameters --------
#if defined(CONFIG_MSTAR_X14) 
	#define XHC_UTMI_EYE_2C	(0x10)
	#define XHC_UTMI_EYE_2D	(0x02)
	#define XHC_UTMI_EYE_2E	(0x00)
	#define XHC_UTMI_EYE_2F	(0x81)
#endif

#if defined(CONFIG_MSTAR_X14)
	#define XHC_SSPORT_OFFSET	0x430
#endif


#endif	/* _XHCI_MSTAR_H */

