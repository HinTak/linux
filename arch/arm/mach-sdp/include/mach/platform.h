/*
 * arch/arm/mach-sdp/include/mach/platform.h
 *
 *  Copyright (C) 2003-2010 Samsung Electronics
 *  Author: tukho.kim@samsung.com
 *
 */

#ifndef _MACH_PLATFORM_H_
#define _MACH_PLATFORM_H_

#if defined(CONFIG_MACH_FOXAP)
#	include <mach/foxap/foxap.h>
#elif defined(CONFIG_MACH_FOXB)
#	include <mach/foxb/foxb.h>
#elif defined(CONFIG_MACH_GOLFS)
#	include <mach/golfs/golfs.h>
#elif defined(CONFIG_MACH_GOLFP)
#	include <mach/golfp/golfp.h>
#elif defined(CONFIG_MACH_ECHOP)
#	include <mach/echop/echop.h>
#else
#include <mach/irqs.h>
#include <mach/sdp_soc.h>
#endif

#endif

