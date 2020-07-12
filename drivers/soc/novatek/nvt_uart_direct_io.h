#ifndef __NVT_UART_DIRECT_IO_H__
#define __NVT_UART_DIRECT_IO_H__

#define UDIO_RD(off)		readl(udiodev->io_base + (off))
#define UDIO_WR(val, off)	writel((val), udiodev->io_base + (off))
#define UDIO_WR_OR(val, off)			\
do {						\
	u32 reg_val;				\
	reg_val = UDIO_RD(off);			\
	UDIO_WR((reg_val | (val)), (off));	\
} while (0)
#define UDIO_WR_AND(val, off)			\
do {						\
	u32 reg_val;				\
	reg_val = UDIO_RD(off);			\
	UDIO_WR((reg_val & ~(val)), (off));	\
} while (0)

/* UART_C PGIO */
#define UART_GPIO_REG		0xfd100060
#define UART_GPIO_UART_C_CLEAR	0x00FF0000
#define UART_GPIO_UART_C	0x00110000

/* INTERNAL_UART_CONNECT */
#define UART_CONNECT_REG	0xfc040248
#define UART_CONNECT_ENABLE	0x00000001

/* Divisor Latch Access Bit0 */
#define UART_DLAB0		0x00

/* Receive Buffer Register */
#define UART_RBR		0x00
#define UART_RBR_RX		0xFF

/* Transmit Holding Register */
#define UART_THR		0x00

/* Divisor Latch Access Bit1 */
#define UART_DLAB1		0x04

/* Interrupt Enable Register */
#define UART_IER		0x04
#define UART_IER_RDI		0x00000001
#define UART_IER_THRI		0x00000002
#define UART_IER_RLSI		0x00000004
#define UART_IER_ALL_IRQ	0x00000007

/* FIFO Control Register */
#define UART_FCR		0x08
#define UART_FCR_FIFO_ENABLE	0x00000001
#define UART_FCR_RX_FIFO_RESET	0x00000002
#define UART_FCR_TX_FIFO_RESET	0x00000004
#define UART_FCR_RX_TX_RESET	(UART_FCR_RX_FIFO_RESET|UART_FCR_TX_FIFO_RESET)
#define UART_FCR_TRIGGER_LV_1	0x00000000
#define UART_FCR_TRIGGER_LV_4	0x00000040
#define UART_FCR_TRIGGER_LV_8	0x00000080
#define UART_FCR_TRIGGER_LV_14	0x000000C0

/* Interrupt ID Register */
#define UART_IIR		0x08
#define UART_IIR_NO_INT		0x00000001

/* Line Control Register */
#define UART_LCR		0x0C
#define UART_LCR_LENGTH_5BIT	0x00000000
#define UART_LCR_LENGTH_6BIT	0x00000001
#define UART_LCR_LENGTH_7BIT	0x00000002
#define UART_LCR_LENGTH_8BIT	0x00000003
#define UART_LCR_STOP_BIT1	0x00000000
#define UART_LCR_STOP_BIT2	0x00000004
#define UART_LCR_PARITY_ENABLE	0x00000008
#define UART_LCR_PARITY_DISABLE	0x00000000
#define UART_LCR_EVEN_PARITY	0x00000010
#define UART_LCR_ODD_PARITY	0x00000000
#define UART_LCR_STICK_PARITY	0x00000020
#define UART_LCR_NORMAL_PARITY	0x00000000
#define UART_LCR_ALL_SETTINGS	0x0000003F
#define UART_LCR_DLAB		0x00000080

/* Line State Register */
#define UART_LSR		0x14
#define UART_LSR_DR		0x00000001
#define UART_LSR_OE		0x00000002
#define UART_LSR_PE		0x00000004
#define UART_LSR_FE		0x00000008
#define UART_LSR_BI		0x00000010
#define UART_LSR_BRK_ERROR_BITS 0x0000001E
#define UART_LSR_THRE		0x00000020

#endif /* __NVT_UART_DIRECT_IO_H__ */
