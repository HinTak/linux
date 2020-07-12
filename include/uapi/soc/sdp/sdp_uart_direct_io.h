/*
 * Copyright (C) 2019 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SDP_UART_DIRECT_IO_H__
#define __SDP_UART_DIRECT_IO_H__

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

struct sdp_udio_linectrl {
	enum {
		PARITY_NONE,
		PARITY_ODD,
		PARITY_EVEN,
		PARITY_FORCED_ONE,
		PARITY_FORCED_ZERO,
	} parity_mode;
	enum {
		STOPBIT_1BIT,
		STOPBIT_2BIT,
	} stop_bit;
	enum {
		WORD_LENGTH_5BIT,
		WORD_LENGTH_6BIT,
		WORD_LENGTH_7BIT,
		WORD_LENGTH_8BIT,
	} word_length;
};

/*
 * Ioctl definitions
 */
#define SDP_UDIO_API_VERSION	1000

/* Use 'U' as magic number */
#define SDP_UDIO_IOC_MAGIC	'U'

#define SDP_UDIO_IOCRESET	_IO(SDP_UDIO_IOC_MAGIC, 0)

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define SDP_UDIO_IOCT_APIVERSION	_IO(SDP_UDIO_IOC_MAGIC,  1)
#define SDP_UDIO_IOCS_LINECTRL	_IOW(SDP_UDIO_IOC_MAGIC,  2, struct sdp_udio_linectrl)
#define SDP_UDIO_IOCG_LINECTRL	_IOR(SDP_UDIO_IOC_MAGIC,  3, struct sdp_udio_linectrl)
#define SDP_UDIO_IOCT_BAUDRATE	_IO(SDP_UDIO_IOC_MAGIC,  4)
#define SDP_UDIO_IOCQ_BAUDRATE	_IO(SDP_UDIO_IOC_MAGIC,  5)
#define SDP_UDIO_IOCT_LOOPBACK	_IO(SDP_UDIO_IOC_MAGIC,  6)
#define SDP_UDIO_IOCQ_LOOPBACK	_IO(SDP_UDIO_IOC_MAGIC,  7)

#define SDP_UDIO_IOCT_HWQSIZE	_IO(SDP_UDIO_IOC_MAGIC,  8)/* return rx/tx hw queue size */
#define SDP_UDIO_IOCT_TXQSIZE	_IO(SDP_UDIO_IOC_MAGIC,  9)/* return driver tx queue size */
#define SDP_UDIO_IOCT_RXQSIZE	_IO(SDP_UDIO_IOC_MAGIC,  10)/* return driver rx queue size */

// #define SDP_UDIO_IOCSQUANTUM _IOW(SDP_UDIO_IOC_MAGIC,  1, int)
// #define SDP_UDIO_IOCSQSET    _IOW(SDP_UDIO_IOC_MAGIC,  2, int)
// #define SDP_UDIO_IOCTQUANTUM _IO(SDP_UDIO_IOC_MAGIC,   3)
// #define SDP_UDIO_IOCTQSET    _IO(SDP_UDIO_IOC_MAGIC,   4)
// #define SDP_UDIO_IOCGQUANTUM _IOR(SDP_UDIO_IOC_MAGIC,  5, int)
// #define SDP_UDIO_IOCGQSET    _IOR(SDP_UDIO_IOC_MAGIC,  6, int)
// #define SDP_UDIO_IOCQQUANTUM _IO(SDP_UDIO_IOC_MAGIC,   7)
// #define SDP_UDIO_IOCQQSET    _IO(SDP_UDIO_IOC_MAGIC,   8)
// #define SDP_UDIO_IOCXQUANTUM _IOWR(SDP_UDIO_IOC_MAGIC, 9, int)
// #define SDP_UDIO_IOCXQSET    _IOWR(SDP_UDIO_IOC_MAGIC,10, int)
// #define SDP_UDIO_IOCHQUANTUM _IO(SDP_UDIO_IOC_MAGIC,  11)
// #define SDP_UDIO_IOCHQSET    _IO(SDP_UDIO_IOC_MAGIC,  12)

// /*
//  * The other entities only have "Tell" and "Query", because they're
//  * not printed in the book, and there's no need to have all six.
//  * (The previous stuff was only there to show different ways to do it.
//  */
// #define SDP_UDIO_P_IOCTSIZE _IO(SDP_UDIO_IOC_MAGIC,   13)
// #define SDP_UDIO_P_IOCQSIZE _IO(SDP_UDIO_IOC_MAGIC,   14)
/* ... more to come */

#define SDP_UDIO_IOC_MAXNR	10

#endif /* __SDP_UART_DIRECT_IO_H__ */
