#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/cdev.h>

#include "ioctl_usben.h"
#include <soc/sdp/soc.h>
#include <trace/early.h>

#include <linux/usb.h>

#define USB_SET_HIGH			1
#define USB_SET_LOW				0

#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1
#define MICOM_MAX_CTRL_TRY		3

#define TVKEY_RESET_TIME		500		/* msec */

#define TVKEY_JAZZM_SUPPORT_USB_BUS				3
/* USB port for TVKey is not fixed */
#define TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_1	2
#define TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_2	3
#define TVKEY_KANTM_OCM_SUPPORT_USB_BUS			2
#define TVKEY_KANTM_OC_SUPPORT_USB_BUS			2

#define	BUS_NUM_MASK			0xFFFF
#define	PORT_NUM_SHIFT			16
#define	PORT_NUM_MASK			0xFFFF

#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
#define OCM_PLUG				1
#define OCM_UNPLUG				0
#define OCM_MAX_CTRL_TRY		10

#if defined(CONFIG_ARCH_SDP1501)		/* Jazz.M */
#define OCM_USB_ENABLE			6
#elif defined(CONFIG_ARCH_SDP1601)		/* Kant.M */
#define OCM_USB_ENABLE			15
#endif


static int (*ocm_gpio_write)(unsigned int port, unsigned int level);
static int (*ocm_gpio_read)(unsigned int port);
static int *hdmi_isOCMConnected;
static int (*is_ocm_model)(void);
static int cold_boot = 1;
#endif


static int gpio;
static struct cdev usben_gpio_cdev;
static int current_gpio_val;
static dev_t usben_id;
static int *read_status;
static int is_force_low = 0;


struct sdp_micom_msg
{
	int msg_type;
	int length;
	unsigned char msg[10];
};


struct sdp_micom_msg micom_msg[] = {
	[USB_SET_LOW] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length 	= KEY_PACKET_DATA_SIZE,
		.msg[0] 	= 0x38,
		.msg[1] 	= 2,
		.msg[2] 	= 0,
	},

	[USB_SET_HIGH] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length		= KEY_PACKET_DATA_SIZE,
		.msg[0]		= 0x38,
		.msg[1]		= 2,
		.msg[2] 	= 1,
	},
/*
	// It was used for Jazz.L only.
	// This is not used but reserve for the OS upgrade.
	[RESET_USER2_HIGH] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length 	= KEY_PACKET_DATA_SIZE,
		.msg[0] 	= 0x36,
		.msg[1] 	= 0x00,
		.msg[2] 	= 0x0C,
		.msg[3] 	= 0x01,
		.msg[4] 	= 1,
	},
	[RESET_USER2_LOW] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length 	= KEY_PACKET_DATA_SIZE,
		.msg[0] 	= 0x36,
		.msg[1] 	= 0x00,
		.msg[2] 	= 0x0C,
		.msg[3] 	= 0x01,
		.msg[4] 	= 0,
	},

*/
};


unsigned long usb_find_symbol(char *name)
{
	struct kernel_symbol *sym = NULL;

	sym = (void *)find_symbol(name, NULL, NULL, 1, true);
	if(sym)
	{
		return sym->value;
	}
	else
	{
		printk(KERN_ERR "[%s] can not find a symbol [%s]\n", __func__, name);
		BUG();
		return -ENOENT;
	}
}


int send_micom_msg(int value)	
{
	unsigned char cmd = 0;
	char ack = 0, data[4] = {0, };
	int len = 0, ret = 0, cnt = 0;
	int (*micom_fn)(char cmd, char ack, char *data, int len);

	cmd = micom_msg[value].msg[0];
	ack = micom_msg[value].msg[0];

	if( cmd == 0x38 )
	{
		data[0] = micom_msg[value].msg[1];
		data[1] = micom_msg[value].msg[2];

		len = 2;
	}
	else if( cmd == 0x36 )		/* NOT used in MAIN2017 */
	{
		data[0] = micom_msg[value].msg[1];
		data[1] = micom_msg[value].msg[2];
		data[2] = micom_msg[value].msg[3];
		data[3] = micom_msg[value].msg[4];

		len = 4;
	}
	else
	{
		printk(KERN_ERR "[%s]: invalid cmd [0x%c]\n", __func__, cmd);
		return -EINVAL;	
	}

	micom_fn = (void *)usb_find_symbol("sdp_micom_send_cmd_ack");

	do
	{
		ret = micom_fn(cmd, ack, data, len);
		if(ret)
		{
			printk(KERN_ERR "[%s] send msg fail [%d, %d]\n", __func__, value, cnt);
			msleep( 100 );
		}
	} while(ret && ((++cnt) < MICOM_MAX_CTRL_TRY));

	return 0;
}
EXPORT_SYMBOL_GPL(send_micom_msg);


#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
#ifndef CONFIG_BD_CACHE_ENABLED		/* Hawk.A & Kant.M-AV does not have 3G & OCM port */

extern int tztv_sys_is_oc_support(void);
extern int tztv_sys_is_ocm_model(void);


int _ext_tztv_sys_is_oc_support(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_is_oc_support);

	return ret;
}


int _ext_tztv_sys_is_ocm_model(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_is_ocm_model);

	return ret;
}


void set_3G_port(int value)
{
	send_micom_msg(value);
	printk( KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low" );

/*
	// It was used for Jazz.L only.
	// This is not used but reserve for the OS upgrade.
	if(soc_is_jazzl())
	{
		send_micom_msg(RESET_USER2_HIGH);
		printk( KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low" );
		msleep( 1 );
	}
*/
}


void set_OCM_port(unsigned int value)
{
	int cnt = 0;
	unsigned int board_info;

	if(cold_boot)
	{
		trace_early_message("[nicolao] find symbol start\n");
		ocm_gpio_write		= (void *)usb_find_symbol("sdp_ocm_gpio_write");
		ocm_gpio_read		= (void *)usb_find_symbol("sdp_ocm_gpio_read");
		hdmi_isOCMConnected = (void *)usb_find_symbol("tztv_hdmi_isOCMConnected");
		is_ocm_model		= (void *)usb_find_symbol("tztv_sys_is_ocm_model");
		trace_early_message("[nicolao] find symbol end\n");

		cold_boot = 0;
	}

	if(1 == is_ocm_model())
	{
		if(*hdmi_isOCMConnected == OCM_PLUG)
		{
			do
			{
				/* ocm_gpio_write always retrun '0'.
				It has to check whether it was correctly set up using ocm_gpio_read. */
				ocm_gpio_write(OCM_USB_ENABLE, value);
				msleep( 500 );
			} while((value != ocm_gpio_read(OCM_USB_ENABLE)) && ((++cnt) <= OCM_MAX_CTRL_TRY));

			printk( KERN_ERR "[%s] set ocm to %s [%d]\n", __func__, value == 1 ? "high" : "low", cnt );
		}
	}
	else
	{
		printk( KERN_ERR "[%s] it has not OCM\n", __func__ );
	}
}
#endif
#endif


void set_GPIO_port(int value)
{
	gpio_set_value((u32)gpio, value);
	printk( KERN_ERR "[%s] set gpio to %s\n", __func__, value == 1 ? "high" : "low");
}


long usben_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)		/* ditto, number and param for ioctl */
{
	switch(ioctl_num)
	{
		/* for Enterprise */
		case IOCTL_USBEN_SET_FORCE:
			if((ioctl_param != USB_SET_LOW) && (ioctl_param != USB_SET_HIGH))
			{
				printk(KERN_ERR "[%s] wrong msg to set usb power\n", __func__);
				return -EINVAL;
			}

			if (ioctl_param == USB_SET_LOW)
				is_force_low = 1;
			else
				is_force_low = 0;

			/* continue IOCTL_USBEN_SET.. */

		case IOCTL_USBEN_SET:
			if((ioctl_param != USB_SET_LOW) && (ioctl_param != USB_SET_HIGH))
			{
				printk(KERN_ERR "[%s] wrong msg to set usb power\n", __func__);
				return -EINVAL;
			}

			if(is_force_low && ioctl_param == USB_SET_HIGH)
			{
				printk(KERN_ERR "[%s] USB force low mode for hotel tv\n", __func__);
				return 0;
			}

			current_gpio_val = ioctl_param;

			if(ioctl_param)			/* Set USB user port to high */
			{
#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
#ifndef CONFIG_BD_CACHE_ENABLED		/* Hawk.A & Kant.M-AV does not have 3G & OCM port */
				set_3G_port(USB_SET_HIGH);
				set_OCM_port(USB_SET_HIGH);
#endif
#endif
				set_GPIO_port(USB_SET_HIGH);
			}
			else if(!ioctl_param)	/* Set USB user port to low */
			{
#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
#ifndef CONFIG_BD_CACHE_ENABLED		/* Hawk.A & Kant.M-AV does not have 3G & OCM port */
				set_3G_port(USB_SET_LOW);
				set_OCM_port(USB_SET_LOW);
#endif
#endif
				set_GPIO_port(USB_SET_LOW);
			}

			break;		/* End of IOCTL_USBEN_SET */


		case IOCTL_USBEN_GET:
			if( !ioctl_param )
			{
				printk(KERN_ERR "[%s] wrong msg to get\n", __func__);
				return -EINVAL;
			}

			read_status = (int*)ioctl_param;
			copy_to_user(read_status, &current_gpio_val, 8);

			break;		/* End of IOCTL_USBEN_GET */


		case IOCTL_USBEN_RESET:
		{
			/*
				TVKey support port info.

				** PreKant(Jazz.M)
					-. OCM		: 3-1(by micom)

				** Kant.M
					-. OC		: 2-1(by micom)
					-. OCM		: 2-1(by micom)
					-. Built-in	: 2-1(by micom), 3-1(by gpio)
			*/

			int bus_num = -1, port_num = -1;

			bus_num = ioctl_param  & BUS_NUM_MASK;
			port_num = (ioctl_param >> PORT_NUM_SHIFT) & PORT_NUM_MASK;
			printk(KERN_ERR "[%s] reset bus: %d, port: %d\n", __func__, bus_num, port_num);

			if(port_num != 1)		/* Do not use the hub for TVKey */
			{
				printk(KERN_ERR "[%s] invalid port for TVKey dongle\n", __func__);
				return -EINVAL;
			}

#ifdef CONFIG_ARCH_SDP1501			/* PreKant(Jazz.M) */
			if(bus_num != TVKEY_JAZZM_SUPPORT_USB_BUS)
			{
				printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
				return -EINVAL;
			}

			printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

			set_3G_port(USB_SET_LOW);
			msleep(TVKEY_RESET_TIME);
			set_3G_port(USB_SET_HIGH);
#endif

#ifdef CONFIG_ARCH_SDP1601			/* Kant.M */
#ifndef CONFIG_BD_CACHE_ENABLED		/* Kant.M-AV does not support TVKey */
			if((1 == _ext_tztv_sys_is_oc_support()) || (1 == _ext_tztv_sys_is_ocm_model()))
			{
				if(!((bus_num == TVKEY_KANTM_OC_SUPPORT_USB_BUS) || (bus_num == TVKEY_KANTM_OCM_SUPPORT_USB_BUS)))
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}

				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_3G_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_3G_port(USB_SET_HIGH);
			}
			else
			{
				if(!((bus_num == TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_1) || (bus_num == TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_2)))
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}
				
				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				if( bus_num == TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_1 )
				{
					set_3G_port(USB_SET_LOW);
					msleep(TVKEY_RESET_TIME);
					set_3G_port(USB_SET_HIGH);
				}
				else
				{
					set_GPIO_port(USB_SET_LOW);
					msleep(TVKEY_RESET_TIME);
					set_GPIO_port(USB_SET_HIGH);
				}
			}
#endif
#endif
		}
			break;		/* End of IOCTL_USBEN_RESET */

	}

	return 0;
}


static const struct file_operations usben_gpio_fileops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= usben_ioctl,
};


static const struct of_device_id usben_match[] = {
	{ .compatible = "samsung,sdp-usben" },
	{},
};
MODULE_DEVICE_TABLE(of, usben_match);


static int usben_probe(struct platform_device *pdev)
{
	int ret = 0;

	gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
	if(!gpio_is_valid(gpio))
	{
		printk(KERN_ERR "[%s] invalid GPIO\n", __func__);
		return -EINVAL;
	}
	current_gpio_val = 1;

	printk(KERN_INFO "of_node Name = %s\n", (pdev->dev.of_node)->name);
	printk(KERN_INFO "of_node Type = %s\n", (pdev->dev.of_node)->type);

	usben_id = MKDEV(MAJOR_NUM, 0);
	ret = register_chrdev_region(usben_id, 1, "USBEN_gpio");
	if(ret < 0)
	{
		printk(KERN_ERR "[%s] problem in getting the major number\n", __func__);
		return ret;
	}

	cdev_init( &usben_gpio_cdev, &usben_gpio_fileops );
	cdev_add( &usben_gpio_cdev, usben_id, 1 );

#if defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
#ifndef CONFIG_BD_CACHE_ENABLED		/* Hawk.A & Kant.M-AV does not have 3G & OCM port */
	/* Enable 3G port to high*/
	set_3G_port(USB_SET_HIGH);
#endif
#endif

	ret = gpio_request((u32)gpio, "usb_enable");
	if(ret)
	{
		if(ret != -EBUSY)
		{
			dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
		}
		return ret;
	}

	/* Set GPIO port to high */
	gpio_direction_output((u32)gpio, 0);
	set_GPIO_port(USB_SET_HIGH);

	return 0;
}


static struct platform_driver usben_driver = {
	.probe	= usben_probe,
	.driver	= {
		.name			= "sdp-usben",
		.owner			= THIS_MODULE,
		.bus			= &platform_bus_type,
		.of_match_table	= of_match_ptr(usben_match),
	},
};


static int __init usben_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&usben_driver);

	return ret;
}


static void __exit usben_exit(void)
{
	cdev_del(&usben_gpio_cdev);
	unregister_chrdev_region(usben_id, 1);
	platform_driver_unregister(&usben_driver);
}


MODULE_ALIAS("platform:sdp-usben");
module_init(usben_init);
module_exit(usben_exit);
MODULE_LICENSE("GPL");

