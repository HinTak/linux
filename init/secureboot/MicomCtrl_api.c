#include <linux/termios.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include "include/Secureboot.h"

#define MICOM_DEV_NAME  CONFIG_MICOM_TTY_PATH
#define BAUDRATE B115200

//extern long sys_read(unsigned int fd, char *buf, long count);
//extern int printk(const char * fmt, ...);
//extern void * memcpy(void *, const void *, unsigned int);
//extern long sys_close(unsigned int fd);
//extern long sys_open(const char *filename, int flags, int mode);
//extern long sys_write(unsigned int fd, const char __attribute__((noderef, address_space(1))) *buf, long count);                              
//extern void msleep(unsigned int msecs);  

static void bzero(void * buf,int size)
{
	char * p=buf;
	int i;
	for(i=0;i<size;i++)
	{
		*p=0;
		p++;
	}
}

static int SerialInit(char *dev)
{
	int fd;
	struct termios newtio;
	
	fd = sys_open(dev, O_RDWR | O_NOCTTY,0 );//MS PARK
	if (fd <0) 
	{
		printk("Error.............. %s\n", dev);
		return (-1);
	}

	bzero((void *)&newtio, sizeof(newtio));

	/* 115200 BPS, Data 8 bit, 1 Stop Bit, NO flow control, No parity. */
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = OPOST;

	/* set input mode (non-canonical, no echo,...) */
	newtio.c_lflag = 0;

	newtio.c_cc[VTIME]    = 0;   
	// newtio.c_cc[VMIN]     = 1;   
	newtio.c_cc[VMIN]     = 0;   /* non-blocking */

	sys_ioctl(fd, TCFLSH, 0);
	sys_ioctl(fd, TCSETS,(unsigned long)&newtio);

	printk("open %s success\n", dev);
	return fd;
}

static int WriteSerialData(int fd, unsigned char *value, unsigned char num)
{
	int res;
	
	res = sys_write((unsigned int)fd, value, num);
	
	if (!res)
	{
		printk("Write Error!!\n");
	}
#ifdef POCKET_TEST
	if(!res)
	{
		printk("[!@#]MI_Err, ------------, -------\r\n");
    }
#endif // #ifdef POCKET_TEST
	return res;
}

static int SerialClose(int fd)
{
	int retVar = 0;

	if(sys_close((unsigned int)fd))
    {
        retVar = 1;
	}
	return retVar;
}

#define SERIAL_LEN	20
int MicomCtrl(unsigned char ctrl, unsigned char arg)
{
	int fd, len;
	char serial_target[SERIAL_LEN];
	unsigned char databuff[9];

	memset(databuff, 0, 9);
	bzero(serial_target, 20);

	len = strlen(MICOM_DEV_NAME);
	//if( len > SERIAL_LEN )
	//	len = SERIAL_LEN;

	strncpy(serial_target, MICOM_DEV_NAME, len);
	serial_target[len] = '\0';
	fd = SerialInit(serial_target);

	databuff[0] = 0xff;
	databuff[1] = 0xff;
	databuff[2] = ctrl;
	databuff[3] = arg;
	databuff[8] += databuff[2];
	databuff[8] += databuff[3];

	WriteSerialData(fd, databuff, 9);
	msleep(10);
	SerialClose(fd);

	return 0;
}

#ifdef CONFIG_MSTAR_X14
extern int micom_cmd(unsigned char command);
#endif

#ifdef CONFIG_ARCH_NVT72668
extern void nt72668_micom_poweroff(void);
extern void nt72668_micom_sendcmd(unsigned char command);
#endif

int micom_poweroff(void)
{
	int i;

	printk("[SABSP] micom_power off 100 times execution start!!!!!\n");
	for(i=0; i<100; i++) {
#ifdef CONFIG_MSTAR_X14
		micom_cmd(0x12); 
#elif CONFIG_ARCH_NVT72668
		nt72668_micom_poweroff();
#else
		MicomCtrl(0x12, 0);
#endif
		mdelay(10); // 10ms spec in
	}
	printk("[SABSP] micom_power off 100 times execution is finished!!!!!\n");
	msleep(3000);
	panic("[SABSP] micom_power off 100 times execution is failed!!!!!\n");
	return 0;
}
EXPORT_SYMBOL(micom_poweroff);

int micom_reboot(void)
{
	int i;

	printk("[SABSP] micom_reboot 100 times execution start!!!!!\n");
	for(i=0; i<100; i++) {
#ifdef CONFIG_MSTAR_X14
		micom_cmd(0x1d);	
#elif CONFIG_ARCH_NVT72668
		nt72668_micom_sendcmd(0x1d);
#else
		MicomCtrl(0x1d, 0);	
#endif
		mdelay(10); // 10ms spec in
	}

	msleep(3000);
	panic("[SABSP] micom_reboot 100 times execution is finished!!!!!\n");
	return 0;
}
