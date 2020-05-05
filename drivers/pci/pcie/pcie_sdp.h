/*
 * linux/..../sdp_pcie.h
 *
 * Author: Samsung Electronics Co, tukho.kim@samsung.com
 *
 */

#ifndef __SDP_PCIE_H
#define __SDP_PCIE_H

#define IRQ_PCI 113//IRQ_SPI(81)


/*	-------------------	*/
/* 	Define BASE Address 	*/
/*	-------------------	*/

#define PA_PCIE_BASE		(0x18840000UL)	

/*	-------------------	*/
/* 	Define GPR Address 	*/
/*	-------------------	*/

#define PCIE_GPR_BASE				0x18850000
#define MAX_PL_SIZE			0                                // max payload size setting 3bit.
#define MAX_READ_REQ_SIZE	2                                // max read request size setting 3bit.
                                                             // 000:  128 Bytes  001:  256 Bytes
                                                             // 010:  512 Bytes  011: 1024 Bytes
                                                             // 100: 2048 Bytes  101: 4096 Bytes
#define SW_RESET1				0x180908f4

#define PCIE_ST_CFGIO_INT 	0x1
#define PCIE_ST_MEM_INT 	0x2
#define PCIE_ST_DMA_INT		0x3

#ifdef __KERNEL__
#include <asm/types.h>
#else
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char u8;
#endif

#endif /* __PCIE_SDP_H */

