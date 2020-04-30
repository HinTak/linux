#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/delay.h>

#define	USBEN_IOCTL_MINOR 253

#define USBEN_MAGIC_KEY		0xEAB
#define USBDIS_MAGIC_KEY	0xDAB

#ifdef CONFIG_ARCH_SDP1404
#include <mach/soc.h>
#include <mach/map.h>
#endif


#ifdef CONFIG_ARCH_NVT72668
#include "mach/motherboard.h"
extern unsigned int Ker_chip;

unsigned int* reg_base;
unsigned int* reg_base2;
unsigned int* reg_base4;
unsigned int* reg_base5;

#define GPIO_SIZE		0x1000		/* dummy value */
#define GPIO_ADDR		0xFD100400	/* dummy value */

#define GPIO_SIZE		0x1000		/* dummy value */
#define GPIO_ADDR2		0xFD0F0000	/* dummy value */

#define GPIO_ADDRNT14M		0xFD0F0000
#define GPIO_ADDRNT14M_2	0xFD110000
#endif

static DEFINE_MUTEX(usben_ioctl_lock);

#ifdef CONFIG_ARCH_NVT72668
#define MMIO(x)				*(volatile unsigned int *)((char *)reg_base + (x))
#define MMIO2(x)			*(volatile unsigned int *)((char *)reg_base2 + (x))
#define MMIO4(x)			*(volatile unsigned int *)((char *)reg_base4 + (x))
#define MMIO5(x)			*(volatile unsigned int *)((char *)reg_base5 + (x))
#endif

/* Architecture Specific Define */
static long usben_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

	int ret = -1;
	mutex_lock(&usben_ioctl_lock);

	switch( cmd )
	{
		case USBEN_MAGIC_KEY :
#ifdef CONFIG_ARCH_SDP1404
			if(get_sdp_board_type() == SDP_BOARD_AV)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X11250000,0x1000);
				/* P12.2/12.3 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 874;*p_con |= 0x3 << 8;
				p_con = (volatile unsigned int *)p_usb_pg + 874;*p_con |= 0x3 << 12;
				p_con = (volatile unsigned int *)p_usb_pg + 875;*p_con |= 1 << 2;
				p_con = (volatile unsigned int *)p_usb_pg + 875;*p_con |= 1 << 3;
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1412)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X10DF8000,0x1000);
				/* P3.0 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 59;*p_con |= 0x3;
				p_con = (volatile unsigned int *)p_usb_pg + 60;*p_con |= 1;
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1406)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X005C1000,0x1000);
				/* P13.1 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 109;*p_con |= 0x3 << 4;
				p_con = (volatile unsigned int *)p_usb_pg + 110;*p_con |= 1 << 1;
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARM_SDP1304_CPUFREQ)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X18090000,0x1000);
				/* P0.3 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 859;*p_con |= 0x3 << 12;
				p_con = (volatile unsigned int *)p_usb_pg + 860;*p_con |= 1 << 3;
				/* P9.4 USB_HUB1_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 886;*p_con |= 0x3 << 16;
				p_con = (volatile unsigned int *)p_usb_pg + 887;*p_con |= 0x1 << 4;
				/* P9.5 USB_HUB2_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 886;*p_con |= 0x3 << 20;
				p_con = (volatile unsigned int *)p_usb_pg + 887;*p_con |= 0x1 << 5;
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARM_SDP1302_CPUFREQ)
                        {
                                unsigned int * p_usb_pg;
                                volatile unsigned int * p_con;
                                p_usb_pg = (unsigned int *)ioremap_nocache(0X10090000,0x1000);
                                /*  USB_ENABLE */
				/* For LFD */
                                p_con = (volatile unsigned int *)p_usb_pg + 838;*p_con |= 0x3 << 4;
                                p_con = (volatile unsigned int *)p_usb_pg + 839;*p_con |= 0x1 << 1;
				
				/* For TV */
                                p_con = (volatile unsigned int *)p_usb_pg + 847;*p_con |= 0x3 << 4;
                                p_con = (volatile unsigned int *)p_usb_pg + 848;*p_con |= 0x1 << 1;

                                /* P14.1 USB_HUB_NRESET */
                                p_con = (volatile unsigned int *)p_usb_pg + 874;*p_con |= 0x3 << 4;
                                p_con = (volatile unsigned int *)p_usb_pg + 875;*p_con |= 0x1 << 1;

                                iounmap(p_usb_pg);
                        }
                        ret = 0;
#elif defined(CONFIG_ARCH_SDP1106)
			*(volatile unsigned int*)0xfe090d00 |= (0x3 << 4);
			*(volatile unsigned int*)0xfe090d04 |= (0x1 << 1);
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1202)
			*(volatile unsigned int*)0xfe090d48 |= (0x3 << 4);
			*(volatile unsigned int*)0xfe090d4c |= (0x1 << 1);
			*(volatile unsigned int*)0xfe090dcc |= (0x3 << 20);
			*(volatile unsigned int*)0xfe090dd0 |= (0x1 << 5);
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1207)
			*(volatile unsigned int*)0xfe090c9c |= (0x3 << 16);
			*(volatile unsigned int*)0xfe090ca0 |= (0x1 << 4);
			*(volatile unsigned int*)0xfe090c90 |= (0x3 << 28);
			*(volatile unsigned int*)0xfe090c94 |= (0x1 << 7);
			ret = 0;
#elif defined(CONFIG_ARCH_NVT72668)
        #if defined(CONFIG_NVT72658_USBEN_VBUS_CONTROL)
                        (MMIO4(0x224)) = MMIO4(0x224) & 0xFFF0FFFF;     //REG_MUXSEL_1
                        MMIO4(0x208) = MMIO4(0x208) | (1 << 12);        //REG_IODIR
                        MMIO4(0x204) = (1 << 12);                       //REG_SET
                        ret = 0;
        #else
			if(Ker_chip == EN_SOC_NT72656)
			{		
				MMIO4(0x224) &= ~(0xFF << 16);
				MMIO5(0x224) &= ~(0xFF << 16);

				MMIO4(0x220) &= ~(0xFF << 16);
				MMIO5(0x220) &= ~(0xFF << 16);

				MMIO4(0x208) |= 1<<4 | 1<<5 | 1<<12 |1<<13;
				MMIO4(0x204) = 1<<4 | 1<<5 | 1<<12 |1<<13;

				MMIO5(0x208) |= 1<<4 | 1<<5 | 1<<12 |1<<13;
				MMIO5(0x204) = 1<<4 | 1<<5 | 1<<12 |1<<13;
			}
			else if(Ker_chip == EN_SOC_NT72668)
			{
				MMIO(0x20) &= ~ (1<<14 | 1<<15 | 1<< 16 | 1<<17);
				MMIO(0x8) |= 1<<20 | 1<<21 | 1<<22 | 1<<23 | 1<<24;
				MMIO(0x4) = 1<<20 | 1<<21 | 1<<22 | 1<<23 | 1<<24;
			}
			ret = 0;
	#endif
#endif
			ret = 0;
			break;
		case USBDIS_MAGIC_KEY :
#ifdef CONFIG_ARCH_SDP1404
			if(get_sdp_board_type() == SDP_BOARD_AV)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X11250000,0x1000);
				/* P12.2/12.3 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 874;*p_con &=(unsigned int)( ~(0x3 << 8));
				p_con = (volatile unsigned int *)p_usb_pg + 874;*p_con &=(unsigned int)( ~(0x3 << 12));
				p_con = (volatile unsigned int *)p_usb_pg + 875;*p_con &=(unsigned int) (~(1 << 2));
				p_con = (volatile unsigned int *)p_usb_pg + 875;*p_con &= (unsigned int)(~(1 << 3));
				iounmap(p_usb_pg);	

			}
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1412)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X10DF8000,0x1000);
				/* P3.0 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 59;*p_con &=(unsigned int)(~(0x3));
				p_con = (volatile unsigned int *)p_usb_pg + 60;*p_con &=(unsigned int)(~(1));
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1406)
			{
				unsigned int * p_usb_pg;
				volatile unsigned int * p_con;
				p_usb_pg = (unsigned int *)ioremap_nocache(0X005C1000,0x1000);
				/* P13.1 USB_ENABLE */
				p_con = (volatile unsigned int *)p_usb_pg + 109;*p_con &=(unsigned int)(~(0x3 << 4));
				p_con = (volatile unsigned int *)p_usb_pg + 110;*p_con &=(unsigned int)(~(1 << 1));
				iounmap(p_usb_pg);	
			}
			ret = 0;
#elif defined(CONFIG_ARCH_SDP1106)
			*(volatile unsigned int*)0xfe090d00 &= ~(0x3 << 4);
			*(volatile unsigned int*)0xfe090d04 &= ~(0x1 << 1);
                        ret = 0;
#elif defined(CONFIG_ARCH_SDP1202)
                        *(volatile unsigned int*)0xfe090d48 &= ~(0x3 << 4);
                        *(volatile unsigned int*)0xfe090d4c &= ~(0x1 << 1);
                        *(volatile unsigned int*)0xfe090dcc &= ~(0x3 << 20);
                        *(volatile unsigned int*)0xfe090dd0 &= ~(0x1 << 5);
                        ret = 0;
#elif defined(CONFIG_ARCH_SDP1207)
                        *(volatile unsigned int*)0xfe090c9c &= ~(0x3 << 16);
                        *(volatile unsigned int*)0xfe090ca0 &= ~(0x1 << 4);
                        *(volatile unsigned int*)0xfe090c90 &= ~(0x3 << 28);
                        *(volatile unsigned int*)0xfe090c94 &= ~(0x1 << 7);
                        ret = 0;
#elif defined(CONFIG_ARCH_NVT72668)
        #if defined(CONFIG_NVT72658_USBEN_VBUS_CONTROL)
                        (MMIO4(0x224)) = MMIO4(0x224) & 0xFFF0FFFF;     //REG_MUXSEL_1
                        MMIO4(0x208) = MMIO4(0x208) | (1 << 12);        //REG_IODIR
                        MMIO4(0x200) = (1 << 12);                       //REG_CLEAR
                        ret = 0;
        #else
			if(Ker_chip == EN_SOC_NT72656)
			{
				MMIO4(0x200) = 1<<4 | 1<<5 | 1<<12 |1<<13;
				MMIO5(0x200) = 1<<4 | 1<<5 | 1<<12 |1<<13;
			}
			else if(Ker_chip == EN_SOC_NT72668)
			{
				MMIO(0) = 1<<20 | 1<<21 | 1<<22 | 1<<23 | 1<<24;
			}
			ret = 0;
	#endif
#endif
			ret = 0;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&usben_ioctl_lock);
	return ret;
}

static const struct file_operations usben_ioctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = usben_ioctl,
	.compat_ioctl = usben_ioctl,
};

static struct miscdevice usben_ioctl_misc = {
	.minor = USBEN_IOCTL_MINOR,
	.name = "usben_ioctl",
	.fops = &usben_ioctl_fops,
};

static int __init usben_ioctl_init(void)
{
	int ret = -1;


#ifdef CONFIG_ARCH_NVT72668
	reg_base = ioremap_nocache(GPIO_ADDR, GPIO_SIZE);
	reg_base2 = ioremap_nocache(GPIO_ADDR2, GPIO_SIZE);
	reg_base4 = ioremap_nocache(GPIO_ADDRNT14M, GPIO_SIZE);
	reg_base5 = ioremap_nocache(GPIO_ADDRNT14M_2, GPIO_SIZE);

	if(unlikely(!reg_base))
		goto fail;
#endif

	ret = misc_register(&usben_ioctl_misc);

	if (unlikely(ret))
		goto fail;

#ifdef CONFIG_ARCH_NVT72668
	usben_ioctl(NULL, USBEN_MAGIC_KEY, 0);
#endif

	return 0;

fail: 
	printk(KERN_ERR "[usben_ioctl] failed to register misc device!\n");
	return ret;
}

static void __exit usben_ioctl_exit(void)
{
	int ret;

	ret = misc_deregister(&usben_ioctl_misc);
	if (unlikely(ret))
		printk(KERN_ERR "[usben_ioctl] failed to unregister misc device!\n");
}

module_init(usben_ioctl_init);
module_exit(usben_ioctl_exit);
