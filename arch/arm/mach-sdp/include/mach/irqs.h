/*
 * arch/arm/mach-sdp/include/mach/irqs.h
 *
 * Copyright (C) 2010 SAMSUNG ELECTRONICS  
 * Author : tukho.kim@samsung.com
 *
 */

#ifndef _MACH_IRQS_H_
#define _MACH_IRQS_H_

#if defined(CONFIG_ARCH_SDP1202)
#   include <mach/foxap/irqs-sdp1202.h>
#elif defined(CONFIG_ARCH_SDP1207)
#   include <mach/foxb/irqs-sdp1207.h>
#elif defined(CONFIG_ARCH_SDP1302)
#   include <mach/golfs/irqs-sdp1302.h>
#elif defined(CONFIG_ARCH_SDP1304)
#   include <mach/golfp/irqs-sdp1304.h>
#elif defined(CONFIG_ARCH_SDP1307)
#   include <mach/golfp/irqs-sdp1307.h>
#elif defined(CONFIG_ARCH_SDP1106)
#   include <mach/echop/irqs-sdp1106.h>
#else
//#error "Not defined IRQ."
//#define NR_IRQS			256
#   include <mach/foxap/irqs-sdp1202.h>
#endif

#endif

