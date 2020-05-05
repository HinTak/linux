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
#include <../include/SpecialItemEnum_Base.h>

#include "ioctl_usben.h"

#define USB_SET_HIGH			1
#define USB_SET_LOW				0

#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1
#define MICOM_MAX_CTRL_TRY		3

#define TVKEY_RESET_TIME		500		// msec
#define TVKEY_BUILTIN_USB_1		3
#define TVKEY_BUILTIN_USB_2		4

#define	BUS_NUM_MASK			0xFFFF
#define	PORT_NUM_SHIFT			16
#define	PORT_NUM_MASK			0xFFFF

static int gpio;
static int dongle_gpio;
static struct cdev usben_gpio_cdev;
static int current_gpio_val;
static dev_t usben_id;
static int *read_status;

#ifdef CONFIG_ARCH_NVT72172		// Kant.S	
static int (*UsbKfactory_drv_get_data_fn)(int id, int *val);
static int num_usb_port = 0;
#endif


// Cause dongle usb is usually used for Wifi
// So it should be controlled by STBC
// But in some case, this USB port can also be controlled in arm side

// More, in some case, like seret, don't have tztv-micom module
// Which means GPIO inside STBC should not be controlled here
// So add a new kernel config "CONFIG_USB_DONGLE_BY_STBC" to turn on/off STBC cmd
#ifdef CONFIG_USB_DONGLE_BY_STBC
#define CTM_3G_DONGLE_ENABLE 0x38

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
		.msg[0] 	= CTM_3G_DONGLE_ENABLE,
		.msg[1] 	= 2,
		.msg[2] 	= 0,
	},
	[USB_SET_HIGH] = {
		.msg_type	= MICOM_NORMAL_DATA,
		.length		= KEY_PACKET_DATA_SIZE,
		.msg[0]		= CTM_3G_DONGLE_ENABLE,
		.msg[1]		= 2,
		.msg[2] 	= 1,
	},
};

static unsigned long usb_find_symbol(char *name)
{
	struct kernel_symbol *sym = NULL;

	sym = (void *)find_symbol(name, NULL, NULL, 1, true);
	if (sym) {
		return sym->value;
	}
	else {
		printk(KERN_ERR "[%s] can not find a symbol [%s]\n", __func__, name);
		BUG();
		return -ENOENT;
	}
}

static int send_micom_msg(int value)	
{
	if (micom_msg[value].msg[0] == CTM_3G_DONGLE_ENABLE) {
		int ret = 0, cnt = 0;
		char data[4] = { micom_msg[value].msg[1], micom_msg[value].msg[2],};

		int (*micom_fn)(char cmd, char ack, char *data, int len);
		micom_fn = (void *)usb_find_symbol("sdp_micom_send_cmd_ack");
		do
		{
			if ((ret = micom_fn(micom_msg[value].msg[0], micom_msg[value].msg[0], data, 2))) {
				printk(KERN_ERR "[%s] send msg fail [%d, %d]\n", __func__, value, cnt);
				msleep(100);
			}
		} while(ret && ((++cnt) < MICOM_MAX_CTRL_TRY));
	}
	return 0;
}
#endif // CONFIG_USB_DONGLE_BY_STBC

void set_3G_port(int value)
{
#ifdef CONFIG_USB_DONGLE_BY_STBC
	send_micom_msg(value);
	printk(KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low");
#else
    if (gpio_is_valid(dongle_gpio)) {
		gpio_set_value((u32)dongle_gpio, value);
		printk(KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low");
	}
#endif
}

void set_GPIO_port(int value)
{
	gpio_set_value((u32)gpio, value);
	printk( KERN_ERR "[%s] set gpio to %s\n", __func__, value == 1 ? "high" : "low");
}

// ditto, number and param for ioctl
long usben_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)	
{
#ifdef CONFIG_ARCH_NVT72172		// Kant.S	
	if (UsbKfactory_drv_get_data_fn == NULL) {	
		UsbKfactory_drv_get_data_fn = (void *)usb_find_symbol("kfactory_drv_get_data");
	}
	if ((num_usb_port == 0) && (UsbKfactory_drv_get_data_fn != NULL)) {
		UsbKfactory_drv_get_data_fn(ID_NUM_OF_USBPORT, &num_usb_port);
	}
	printk(KERN_ERR "[%s] [%d] num_usb_port = %d [ factory addr = %p]", __func__, __LINE__, num_usb_port, UsbKfactory_drv_get_data_fn);
#endif	
	switch(ioctl_num)
	{
		case IOCTL_USBEN_SET:
			if(ioctl_param != USB_SET_LOW && ioctl_param != USB_SET_HIGH) {
				printk(KERN_ERR "[%s] wrong msg to set usb power\n", __func__);
				return -EINVAL;
			}

			current_gpio_val = ioctl_param ? USB_SET_HIGH : USB_SET_LOW;
			set_3G_port(current_gpio_val);
			set_GPIO_port(current_gpio_val);
			break;
		case IOCTL_USBEN_GET:
			if (!ioctl_param) {
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
			//													//
			// ** Kant.SU										//
			// -. Built-in	: 3-1(by gpio), 4-1(by micom)		//
			//==================================================//
			int bus_num  = ioctl_param  & BUS_NUM_MASK;
			int port_num = (ioctl_param >> PORT_NUM_SHIFT) & PORT_NUM_MASK;

			printk(KERN_ERR "[%s] reset bus: %d, port: %d\n", __func__, bus_num, port_num);

			// Do not use the hub for TVKey
			if (port_num != 1) {
				printk(KERN_ERR "[%s] invalid port for TVKey dongle\n", __func__);
				return -EINVAL;
			}

#ifdef CONFIG_ARCH_NVT72172		// Kant.S
			if( (bus_num == TVKEY_BUILTIN_USB_1) ||((bus_num == TVKEY_BUILTIN_USB_2) && (num_usb_port == 1))){
#else							// Kant.SU
			if (bus_num == TVKEY_BUILTIN_USB_2) {
#endif
				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_3G_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_3G_port(USB_SET_HIGH);
			}
#ifdef CONFIG_ARCH_NVT72172		// Kant.S
			else if (bus_num == TVKEY_BUILTIN_USB_2) {
#else							// Kant.SU
			else if (bus_num == TVKEY_BUILTIN_USB_1) {
#endif
				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_GPIO_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_GPIO_port(USB_SET_HIGH);
			}
			else {
				printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
				return -EINVAL;
			}
			break;
		}
		// Only set arm side GPIO
		case IOCTL_USBEN_SET_ONLY:
			set_GPIO_port(ioctl_param ? USB_SET_HIGH : USB_SET_LOW);
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

#ifdef CONFIG_ARCH_NVT72172		// Kant.S	
	UsbKfactory_drv_get_data_fn = NULL;
	printk(KERN_ERR "[%s] [%d] [ addr = %p]", __func__, __LINE__,  UsbKfactory_drv_get_data_fn);
#endif

	gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
	if (!gpio_is_valid(gpio)) {
		printk(KERN_ERR "[%s] invalid GPIO\n", __func__);
		return -EINVAL;
	}

#ifndef CONFIG_USB_DONGLE_BY_STBC
    dongle_gpio = of_get_named_gpio( pdev->dev.of_node, "samsung,usb-don-enable", 0 );
    if (!gpio_is_valid(dongle_gpio))
        printk(KERN_INFO "%s no invalid dongle gpio\n", __func__ );
#endif

	printk(KERN_INFO "of_node Name = %s\n", (pdev->dev.of_node)->name);
	printk(KERN_INFO "of_node Type = %s\n", (pdev->dev.of_node)->type);

	usben_id = MKDEV(MAJOR_NUM, 0);
	ret = register_chrdev_region(usben_id, 1, "USBEN_gpio");
	if (ret < 0) {
		printk(KERN_ERR "[%s] problem in getting the major number\n", __func__);
		return ret;
	}

	cdev_init(&usben_gpio_cdev, &usben_gpio_fileops);
	cdev_add(&usben_gpio_cdev, usben_id, 1);

	ret = gpio_request((u32)gpio, "usb_enable");
	if (ret) {
		if (ret != -EBUSY)
			dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
		return ret;
	}
	gpio_direction_output((u32)gpio, 0);

#ifndef CONFIG_USB_DONGLE_BY_STBC
	if (gpio_is_valid(dongle_gpio)) {
		ret = gpio_request((u32)dongle_gpio, "usb_don_enable");
		if (ret) {
			if(ret != -EBUSY)
				dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
			return ret;
		}
		gpio_direction_output((u32)dongle_gpio, 0);
	}
#endif

	// Set GPIO port to high
	current_gpio_val = USB_SET_HIGH;
	set_GPIO_port(current_gpio_val);
	set_3G_port(current_gpio_val);
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
	return platform_driver_register(&usben_driver);
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

