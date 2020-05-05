#include <linux/module.h>
#include <linux/io.h>
#include "mach/motherboard.h"
#include "mach/pm.h"

extern unsigned long get_ahb_clk(void);
extern struct nt72668_soc_pm_info_struct *pm_info;
unsigned Gb_uart0_addr,Gb_uart1_addr;
void nt72668_micom_uart0_putc(unsigned char c);
char nt72668_micom_uart0_getc(void);
void nt72668_micom_uart0_sendcmd(unsigned char *s, unsigned char size);
void nt72668_micom_uart0_receivecmd(unsigned char *s, unsigned char size);
//void nt72668_micom_sendcmd(unsigned char command);

#define RETRY_CNT 3
#define Baudrate 115200
#define UART_BASE_ADDRESS 0xfd092000

void nt72668_micom_uart0_init(void)
{	
	unsigned temp,old_LCR,divisor;
	unsigned long ahb_freq;
	Gb_uart1_addr = ioremap(UART_BASE_ADDRESS, 0x2);
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

static void uart0_wait_tx(void)
{
	while ((readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LSR)) & NT72668_UART_LSR_THREMPTY) == 0) {
		/* nop */
	}
}

void nt72668_micom_uart0_putc(unsigned char c)
{
	uart0_wait_tx();
	writel(c, (volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_RBR));
}

static void uart0_wait_rx(void)
{
	while ((readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_LSR)) & NT72668_UART_LSR_DATAREADY) == 0) {
		/* nop */
	}
}

char nt72668_micom_uart0_getc(void)              
{
	nt72668_micom_uart0_init();
	uart0_wait_rx();
	return (char)(readl((volatile void __iomem *)(Gb_uart1_addr+NT72668_UARTx_RBR)) & 0xff);
}

void nt72668_micom_uart0_sendcmd(unsigned char *s, unsigned char size)
{
	nt72668_micom_uart0_init();
	
	while (size) {
		nt72668_micom_uart0_putc (*s);
		size--;
		s++;
	}
}

void nt72668_micom_uart0_receivecmd(unsigned char *s, unsigned char size)
{
	nt72668_micom_uart0_init();
	
	while (size) {
		*s = nt72668_micom_uart0_getc();
		size--;
		s++;
	}
}
#if 0
void nt72668_micom_sendcmd(unsigned char command)
{
	unsigned char cmd[10];
	int retry = RETRY_CNT;

	/* send micom suspend */
	nt72668_micom_uart0_init();

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
		nt72668_micom_uart0_sendcmd((unsigned char *) cmd, MICOM_CMD_SIZE);
	}	
}
#endif
EXPORT_SYMBOL(nt72668_micom_uart0_init);
EXPORT_SYMBOL(uart0_wait_tx);
EXPORT_SYMBOL(nt72668_micom_uart0_putc);
EXPORT_SYMBOL(uart0_wait_rx);
EXPORT_SYMBOL(nt72668_micom_uart0_getc);
EXPORT_SYMBOL(nt72668_micom_uart0_sendcmd);
EXPORT_SYMBOL(nt72668_micom_uart0_receivecmd);
//EXPORT_SYMBOL(nt72668_micom_sendcmd);
