#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <mach/map.h>
#include <mach/soc.h>

struct sdp_micom_uart {
	volatile unsigned int	ulcon;
	volatile unsigned int	ucon;
	volatile unsigned int	ufcon;
	volatile unsigned int	umcon;

	volatile unsigned int	utrstat;
	volatile unsigned int	uerstat;
	volatile unsigned int	ufstat;
	volatile unsigned int	umstat;

	volatile unsigned int	utxh;
	volatile unsigned int	urxh;
	volatile unsigned int	ubrdiv;
};

static struct sdp_micom_uart *uart;

extern unsigned long SDP_GET_UARTCLK(char mode);

#define MICOM_LONG_CMD		9
#define MICOM_SHORT_CMD		3
#define MICOM_REQ_SLEEP		0x25
#define MICOM_REQ_POWER_OFF	0x12

static void uart_init(void)
{
	uart->ulcon = 0x3;
	uart->ucon = 0x5;
	uart->ufcon = 0x0;	/* fifo disabled */
	uart->umcon = 0x0;
}

static void uart_wait_rx(void)
{
	while ( (uart->utrstat & 0x1) == 0) {
		/* nop */
	}
}

static void uart_wait_tx(void)
{
	while ( (uart->utrstat & 0x2) == 0) {
		/* nop */
	}
}

void sdp_micom_uart_init(unsigned int base, int port)
{	
	void *iomem;
#if 0
	iomem = ioremap(base, 0x100);
	if(!iomem)
		pr_err("suspend micom ioremap failed!!!!\n");
#else
	iomem = (void*)base + DIFF_IO_BASE0;
#endif
	uart = (struct sdp_micom_uart *)(iomem + port * 0x40);
	uart_init();
}

void sdp_micom_uart_putc(unsigned char c)
{
	uart_wait_tx();
	uart->utxh = c;
}

char sdp_micom_uart_getc(void)
{
	uart_wait_rx();
	return uart->urxh;
}

void sdp_micom_uart_sendcmd(unsigned char *s, unsigned char size)
{
	while (size) {
		sdp_micom_uart_putc (*s);
		size--;
		s++;
	}
}

void sdp_micom_uart_receivecmd(unsigned char *s, unsigned char size)
{
	while (size) {
		*s = sdp_micom_uart_getc();
		size--;
		s++;
	}
}

/* timeout = in usec */
int sdp_micom_uart_receivecmd_timeout(unsigned char *s, unsigned char size, int timeout)
{
	/* 115200bsp = 8.68usec / bit */
	while (size > 0) {
		while(!(uart->utrstat & 0x1) && (timeout > 0)) {
			timeout--;
			udelay(1);
		}
		if (timeout < 1)
			break;
		*s = (u8)readl_relaxed(&uart->urxh);
		size--;
		s++;
	}
	return (timeout < 1) ?  -ETIMEDOUT : 0;
}

void sdp_micom_send_byte_cmd(u8 cmd2)
{
	unsigned char cmd[10];

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = cmd2;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = cmd2;
	
	sdp_micom_uart_sendcmd((unsigned char *) cmd, MICOM_LONG_CMD);
}

/* older version: using uart */
#if 0
static void _sdp_micom_req_suspend(void)
{
	int ret;
	unsigned char cmd[10] = {
		0xff, 0xff, MICOM_REQ_SLEEP,
		0, 0, 0, 0, 0,
		MICOM_REQ_SLEEP, };
	unsigned char ack[MICOM_SHORT_CMD];

	sdp_micom_uart_init(sdp_uart_base, sdp_micom_port);

	sdp_micom_uart_sendcmd(cmd, MICOM_LONG_CMD);

	ret = sdp_micom_uart_receivecmd_timeout(ack, MICOM_SHORT_CMD, 20*1000);
	
	if ((ret < 0) || !(ack[0] == 0xff && ack[1] == 0xff && ack[2] == MICOM_REQ_SLEEP)) {
		/* try only 1 more time, we have no time. */
		udelay(1000);
		sdp_micom_uart_sendcmd(cmd, MICOM_LONG_CMD);
	}
}
#endif

void sdp_micom_request_poweroff(void)
{
	sdp_micom_send_byte_cmd(MICOM_REQ_POWER_OFF);
}

void sdp_micom_request_suspend(void)
{
	void *gpio_dat;
	int ind_pin;
	int high_time = 30;
	u32 val;
	
	/* 1. send UART command */
	sdp_micom_send_byte_cmd(MICOM_REQ_SLEEP);

	/* 2. send GPIO */
	if(soc_is_sdp1304()) {
		gpio_dat = (void __iomem *)((void*)0x10B00CFC + DIFF_IO_BASE0);
		ind_pin = 3;
	} else if (soc_is_sdp1302()) {
		gpio_dat = (void __iomem *)((void*)0x10090D28 + DIFF_IO_BASE0);
		ind_pin = 1;
	} else {
		panic("This platform does not support micom suspen.");
		return;
	}
		
	val = readl_relaxed(gpio_dat);
	val |= (1 << ind_pin);
	writel_relaxed(val, gpio_dat);

	while(high_time--) {
		udelay(1000);
	}
	
	val &= ~(1 << ind_pin);
	writel_relaxed(val, gpio_dat);
}

