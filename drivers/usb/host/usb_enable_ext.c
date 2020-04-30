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


#define USB_SET_HIGH			1
#define USB_SET_LOW				0

#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1
#define MICOM_MAX_CTRL_TRY		3

#define TVKEY_RESET_TIME		500		// msec

#define TVKEY_KANTS_SUPPORT_USB_BUS_1			3
#define TVKEY_KANTS_SUPPORT_USB_BUS_2			4

#define	BUS_NUM_MASK			0xFFFF
#define	PORT_NUM_SHIFT			16
#define	PORT_NUM_MASK			0xFFFF

static int gpio;
static int dongle_gpio;
static struct cdev usben_gpio_cdev;
static int current_gpio_val;
static dev_t usben_id;
static int *read_status;


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



void set_3G_port(int value)
{
	gpio_set_value((u32)dongle_gpio, value);	// TODO : It should be removed when the circuit has been modified
	send_micom_msg(value);
	printk( KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low" );
}


void set_GPIO_port(int value)
{
	gpio_set_value((u32)gpio, value);
	printk( KERN_ERR "[%s] set gpio to %s\n", __func__, value == 1 ? "high" : "low");
}


// ditto, number and param for ioctl
long usben_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)	
{
	switch(ioctl_num)
	{
		case IOCTL_USBEN_SET:
			if((ioctl_param != USB_SET_LOW) && (ioctl_param != USB_SET_HIGH))
			{
				printk(KERN_ERR "[%s] wrong msg to set usb power\n", __func__);
				return -EINVAL;
			}

			current_gpio_val = ioctl_param;

			if(ioctl_param)			// Set USB user port to high
			{
				set_3G_port(USB_SET_HIGH);
				set_GPIO_port(USB_SET_HIGH);
			}
			else if(!ioctl_param)	// Set USB user port to low
			{
				set_3G_port(USB_SET_LOW);
				set_GPIO_port(USB_SET_LOW);
			}

			break;


		case IOCTL_USBEN_GET:
			if( !ioctl_param )
			{
				printk(KERN_ERR "[%s] wrong msg to get\n", __func__);
				return -EINVAL;
			}

			read_status = (int*)ioctl_param;
			copy_to_user(read_status, &current_gpio_val, 8);

			break;


		case IOCTL_USBEN_RESET:
		{
			//==================================================//
			// TVKey support port info.							//
			//													//
			// ** Kant.S										//
			// -. Built-in	: 3-1(by micom), 4-1(by gpio)		//
			//==================================================//

			int bus_num = -1, port_num = -1;

			bus_num = ioctl_param  & BUS_NUM_MASK;
			port_num = (ioctl_param >> PORT_NUM_SHIFT) & PORT_NUM_MASK;
			printk(KERN_ERR "[%s] reset bus: %d, port: %d\n", __func__, bus_num, port_num);

			if(port_num != 1)			// Do not use the hub for TVKey
			{
				printk(KERN_ERR "[%s] invalid port for TVKey dongle\n", __func__);
				return -EINVAL;
			}

			if( bus_num == TVKEY_KANTS_SUPPORT_USB_BUS_1 )			// usb 3-1
			{
				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_3G_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_3G_port(USB_SET_HIGH);
			}
			else if( bus_num == TVKEY_KANTS_SUPPORT_USB_BUS_2 )		// usb 4-1
			{
				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_GPIO_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_GPIO_port(USB_SET_HIGH);
			}
			else
			{
				printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
				return -EINVAL;
			}
		}
			break;

	}

	return 0;
}


static const struct file_operations usben_gpio_fileops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= usben_ioctl,
};


static const struct of_device_id usben_match[] = {
	{ .compatible = "samsung,nvt-usben" },
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

	// TODO : It should be removed when the circuit has been modified
    dongle_gpio = of_get_named_gpio( pdev->dev.of_node, "samsung,usb-don-enable", 0 );
    if( !gpio_is_valid(dongle_gpio) )
    {
        printk( KERN_ERR "%s Invalid dongle gpio\n", __func__ );
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

	ret = gpio_request((u32)gpio, "usb_enable");
	if(ret)
	{
		if(ret != -EBUSY)
		{
			dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
		}
		return ret;
	}

	// Set GPIO port to high
	gpio_direction_output((u32)gpio, 0);
	set_GPIO_port(USB_SET_HIGH);


	// TODO : It should be removed when the circuit has been modified
	ret = gpio_request((u32)dongle_gpio, "usb_don_enable");
	if(ret)
	{
		if(ret != -EBUSY)
		{
			dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
		}
		return ret;
	}

	// TODO : It should be replaced with micom control code when the circuit has been modified
	// Set 3G GPIO port to high
	gpio_direction_output((u32)dongle_gpio, 0);
	set_3G_port(USB_SET_HIGH);

	return 0;
}


static struct platform_driver usben_driver = {
	.probe	= usben_probe,
	.driver	= {
		.name			= "nvt-usben",
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

