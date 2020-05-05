
#include <linux/io.h>
#include "mach/motherboard.h"
#include "mach/pm.h"

extern unsigned long get_ahb_clk(void);
extern struct nt72668_soc_pm_info_struct *pm_info;
extern unsigned Gb_uart0_addr,Gb_uart1_addr;
void nt72668_micom_uart_putc(unsigned char c);
char nt72668_micom_uart_getc(void);
void nt72668_micom_uart_sendcmd(unsigned char *s, unsigned char size);
void nt72668_micom_uart_receivecmd(unsigned char *s, unsigned char size);
void nt72668_micom_sendcmd(unsigned char command);

#define RETRY_CNT 3
#define Baudrate 115200

void nt72668_micom_uart_init(void)
{	
	unsigned temp,old_LCR,divisor;
	unsigned long ahb_freq;
	old_LCR = readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LCR));
	temp = old_LCR | 0x80;		
	writel(temp, (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LCR));
	ahb_freq = get_ahb_clk();
	divisor = (ahb_freq ) / (Baudrate * 16);
	writel((divisor & 0x000000ff), (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_DLAB0));
	writel(((divisor & 0x0000ff00)>>8), (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_DLAB1));
	writel(0xc1, (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_FCR));  /* 0xc1: Receiver Trigger Level Select is 11 + FIFO Enable */
	writel(old_LCR, (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LCR));
	return;
}

static void uart_wait_tx(void)
{
	while ((readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LSR)) & NT72668_UART_LSR_THREMPTY) == 0) {
		/* nop */
	}
}

void nt72668_micom_uart_putc(unsigned char c)
{
	uart_wait_tx();
	writel(c, (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_RBR));
}

static void uart_wait_rx(void)
{
	while ((readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LSR)) & NT72668_UART_LSR_DATAREADY) == 0) {
		/* nop */
	}
}

char nt72668_micom_uart_getc(void)              
{
	uart_wait_rx();
	return (char)(readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_RBR)) & 0xff);
}

void nt72668_micom_uart_sendcmd(unsigned char *s, unsigned char size)
{
	while (size) {
		nt72668_micom_uart_putc (*s);
		size--;
		s++;
	}
}

void nt72668_micom_uart_receivecmd(unsigned char *s, unsigned char size)
{
	while (size) {
		*s = nt72668_micom_uart_getc();
		size--;
		s++;
	}
}

void nt72668_micom_poweroff(void)
{
	unsigned char cmd[10];
	
	/* send micom power off */
	nt72668_micom_uart_init();

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = MICOM_REQ_POWER_OFF;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = (unsigned char)(cmd[2]+cmd[3]);
	
	nt72668_micom_uart_sendcmd((unsigned char *) cmd, MICOM_CMD_SIZE);
}

void nt72668_micom_suspend(void)
{
	unsigned char cmd[10];
	int retry = RETRY_CNT;

	/* send micom suspend */
	nt72668_micom_uart_init();

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = MICOM_REQ_SLEEP;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = (unsigned char)(cmd[2]+cmd[3]);
	
	while (retry-- > 0)
	{
		nt72668_micom_uart_sendcmd((unsigned char *) cmd, MICOM_CMD_SIZE);
	}	
}

void nt72668_micom_sendcmd(unsigned char command)
{
	unsigned char cmd[10];
	int retry = RETRY_CNT;

	/* send micom suspend */
	nt72668_micom_uart_init();

	cmd[0] = 0xFF;
	cmd[1] = 0xFF;
	cmd[2] = command;
	cmd[3] = 0x0;
	cmd[4] = 0x0;
	cmd[5] = 0x0;
	cmd[6] = 0x0;
	cmd[7] = 0x0;
	cmd[8] = (unsigned char)(cmd[2]+cmd[3]);
	
	while (retry-- > 0)
	{
		nt72668_micom_uart_sendcmd((unsigned char *) cmd, MICOM_CMD_SIZE);
	}	
}

