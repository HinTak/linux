/*
 * Copyright (C) 2019 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SDP_UART_LLD_H__
#define __SDP_UART_LLD_H__

#define SDP_ULCON			0x00/* uart line control */
#define SDP_UCON			0x04/* uart control */
#define SDP_UFCON			0x08/* uart fifo control */
#define SDP_UMCON			0x0C/* uart modem control */
#define SDP_UTRSTAT			0x10/* uart tx/rx status */
#define SDP_UERSTAT			0x14/* uart rx error status */
#define SDP_UFSTAT			0x18/* uart fifo status */
#define SDP_UMSTAT			0x1C/* uart modem status */
#define SDP_UTXH			0x20/* uart tx buffer */
#define SDP_URXH			0x24/* uart rx buffer */
#define SDP_UBRDIV			0x28/* baud rate divisior */

#define SDP_ULCON_PNONE			(0 << 3)
#define SDP_ULCON_PODD			(4 << 3)
#define SDP_ULCON_PEVEN			(5 << 3)
#define SDP_ULCON_PFORCEDONE	(6 << 3)
#define SDP_ULCON_PFORCEDZERO	(7 << 3)
#define SDP_ULCON_PMASK			(7 << 3)

#define SDP_ULCON_STOPB			(1 << 2)

#define SDP_ULCON_CS5			(0 << 0)
#define SDP_ULCON_CS6			(1 << 0)
#define SDP_ULCON_CS7			(2 << 0)
#define SDP_ULCON_CS8			(3 << 0)
#define SDP_ULCON_CSMASK		(3 << 0)

#define SDP_UCON_ERRIRQEN		(1 << 14)
#define SDP_UCON_TXIRQ2MODE		(1 << 13)
#define SDP_UCON_RXIRQ2MODE		(1 << 12)
#define SDP_UCON_RXFIFO_TOI		(1 << 7)
#define SDP_UCON_LOOPBACK		(1 << 5)
#define SDP_UCON_SBREAK			(1 << 4)
#define SDP_UCON_TXIRQMODE		(1 << 2)
#define SDP_UCON_RXIRQMODE		(1 << 0)

#define SDP_UCON_DEFAULT		(SDP_UCON_RXIRQMODE | \
						SDP_UCON_TXIRQMODE | \
						SDP_UCON_RXFIFO_TOI | \
						SDP_UCON_RXIRQ2MODE | \
						SDP_UCON_ERRIRQEN)

#define SDP_UFCON_TXTRIG0		(0 << 6)
#define SDP_UFCON_TXTRIG4		(1 << 6)
#define SDP_UFCON_TXTRIG8		(2 << 6)
#define SDP_UFCON_TXTRIG12		(3 << 6)

#define SDP_UFCON_RXTRIG4		(0 << 4)
#define SDP_UFCON_RXTRIG8		(1 << 4)
#define SDP_UFCON_RXTRIG12		(2 << 4)
#define SDP_UFCON_RXTRIG16		(3 << 4)

#define SDP_UFCON_RESETBOTH		(3 << 1)
#define SDP_UFCON_RESETTX		(1 << 2)
#define SDP_UFCON_RESETRX		(1 << 1)
#define SDP_UFCON_FIFOMODE		(1 << 0)

#define SDP_UFCON_ENHANCED		(1 << 3)

#define SDP_UFCON_DEFAULT		(SDP_UFCON_FIFOMODE | \
						SDP_UFCON_RXTRIG4 | \
						SDP_UFCON_TXTRIG0)

#define SDP_UFCON2_DEFAULT		(SDP_UFCON_FIFOMODE | (0 << 8) | SDP_UFCON_ENHANCED)

#define SDP_UMCON2_DEFAULT	(4 << 8)


#define SDP_UTRSTAT_ERRI		(1 << 6)
#define SDP_UTRSTAT_TXI			(1 << 5)
#define SDP_UTRSTAT_RXI			(1 << 4)
#define SDP_UTRSTAT_TXE			(1 << 2)
#define SDP_UTRSTAT_TXFE		(1 << 1)
#define SDP_UTRSTAT_RXDR		(1 << 0)

#define SDP_UERSTAT_BREAK		(1 << 3)
#define SDP_UERSTAT_FRAME		(1 << 2)
#define SDP_UERSTAT_PARITY		(1 << 1)
#define SDP_UERSTAT_OVERRUN		(1 << 0)

#define SDP_UERSTAT_ANY			(SDP_UERSTAT_OVERRUN | \
						SDP_UERSTAT_FRAME | \
						SDP_UERSTAT_BREAK)

#define SDP_UFSTAT_TXFIFO_MASK		(0xF << 4)
#define SDP_UFSTAT_TXFIFO_SHIFT		4
#define SDP_UFSTAT_TXFIFO_MAX		15
#define SDP_UFSTAT_RXFIFO_MASK		(0xF << 0)
#define SDP_UFSTAT_TXFIFO_FULL		(1 << 9)
#define SDP_UFSTAT_RXFIFO_FULL		(1 << 8)

#define SDP_UFSTAT2_TXFIFO_MASK		(0x3F << 8)
#define SDP_UFSTAT2_TXFIFO_SHIFT	8
#define SDP_UFSTAT2_TXFIFO_MAX		63
#define SDP_UFSTAT2_RXFIFO_MASK		(0x3F << 0)
#define SDP_UFSTAT2_TXFIFO_FULL		(1 << 14)
#define SDP_UFSTAT2_RXFIFO_FULL		(1 << 6)

#endif /* __SDP_UART_LLD_H__ */
