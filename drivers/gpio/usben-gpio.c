#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <asm/io.h>
#include <asm/delay.h>

#define	USBEN_IOCTL_MINOR 253

#define USBEN_MAGIC_KEY		0xEAB
#define USBDIS_MAGIC_KEY	0xDAB

/* Architecture Specific Define */
#ifndef CONFIG_ARCH_NVT72668
#if defined(CONFIG_MSTAR_PreX14)
#define GPIO_SIZE		0x20000
#define GPIO_ADDR		0x1F000000  /* FOR Mstar X12/PreX14 */
#elif defined(CONFIG_MSTAR_X14)
#define GPIO_SIZE		0x00006000
#define GPIO_ADDR		0x1F200000	/* FOR Mstar X14 */
#else
#define GPIO_SIZE		0x1000		/* dummy value */
#define GPIO_ADDR		0x10090000	/* dummy value */
#endif
#else
#include "mach/motherboard.h"
extern unsigned int Ker_chip;

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
unsigned int* reg_base;

static DEFINE_MUTEX(usben_ioctl_lock);

#define MMIO(x)			*(volatile unsigned int *)((char *)reg_base + (x))

#ifdef CONFIG_ARCH_NVT72668
#define MMIO2(x)			*(volatile unsigned int *)((char *)reg_base2 + (x))
#define MMIO4(x)			*(volatile unsigned int *)((char *)reg_base4 + (x))
#define MMIO5(x)			*(volatile unsigned int *)((char *)reg_base5 + (x))

#endif

static long usben_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -1;
	mutex_lock(&usben_ioctl_lock);

	switch( cmd )
	{
		case USBEN_MAGIC_KEY :
#if defined(CONFIG_MSTAR_PreX14)
			/* USB_EN(GPIO8) - Setting USB_EN as Output */
			MMIO(0x1e00) &= ~(0x1 << 0);
			udelay(2);
			MMIO(0x1e00) |= (0x1 << 1);	 /* enable */
			ret = 0;
#elif defined(CONFIG_MSTAR_X14)
			MMIO(0x56c4) &= ~(0x1 << 9);
			udelay(2);
			MMIO(0x56c4) |= (0x1 << 8);	 /* enable */
			MMIO(0x5634) &= ~(0x1 << 1);
			udelay(2);
			MMIO(0x5634) |= (0x1 << 0);	 /* enable */
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
			break;
		case USBDIS_MAGIC_KEY :
#if defined(CONFIG_MSTAR_EDISON) || defined(CONFIG_MSTAR_PreX14)
			MMIO(0x1e00) &= ~(0x1 << 0);
			udelay(2);
			MMIO(0x1e00) &= ~(0x1 << 1); /* disable */
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

	reg_base = ioremap_nocache(GPIO_ADDR, GPIO_SIZE);
#ifdef CONFIG_ARCH_NVT72668
	reg_base2 = ioremap_nocache(GPIO_ADDR2, GPIO_SIZE);
	reg_base4 = ioremap_nocache(GPIO_ADDRNT14M, GPIO_SIZE);
	reg_base5 = ioremap_nocache(GPIO_ADDRNT14M_2, GPIO_SIZE);
#endif
	if(unlikely(!reg_base))
		goto fail;

	ret = misc_register(&usben_ioctl_misc);

	if (unlikely(ret))
		goto fail;

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
