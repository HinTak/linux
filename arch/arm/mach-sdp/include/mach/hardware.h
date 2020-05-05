/*
 *  arch/arm/mach-sdp/include/mach/hardware.h
 *
 *  This file contains the hardware definitions of Samsung SDP Platform
 *
 *  Copyright (C) 2003 ARM Limited.
 *  Copyright (C) 2010 Samsung elctronics co.
 *  Author : seongkoo.cheong@samsung.com
 *
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#if 0	/* We don't havce PCI bus yet, modify this when golf-b is out. */
#include <asm/sizes.h>
#include <mach/platform.h>

#define pcibios_assign_all_busses() 	1
#define PCIBIOS_MIN_IO			0x6000
#define PCIBIOS_MIN_MEM 		0x00100000
#endif

#endif

