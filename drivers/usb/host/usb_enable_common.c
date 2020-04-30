/*
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */


#include "ioctl_usben.h"
#include "usb_enable_common.h"


long control_usb_power(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	switch(ioctl_num)
	{
		case IOCTL_USBEN_SET:
		{
			if((ioctl_param == USB_ON) || (ioctl_param == USB_OFF)) {
				set_usb_power(ioctl_param);
			}
			else {
				printk(KERN_ERR "[%s] wrong msg to set usb power\n", __func__);
				return -EINVAL;
			}
		}
			break;

		case IOCTL_USBEN_GET:
		{
			// Not implemented. Because there is no place to use.
			printk(KERN_ERR "[%s] not implemented. because there is no place to use\n", __func__);
		}
			break;

		case IOCTL_USBEN_RESET:
		{
			int bus_num = -1, prt_num = -1;

			bus_num = (ioctl_param & BUS_NUM_MASK);
			prt_num = (ioctl_param >> PORT_NUM_SHIFT) & PORT_NUM_MASK;
			printk(KERN_ERR "[%s] reset bus: %d, port: %d\n", __func__, bus_num, prt_num);

			if(prt_num != 1) {		// Do not support the hub for TVKey
				printk(KERN_ERR "[%s] invalid port for TVKey dongle\n", __func__);
				return -EINVAL;
			}

			reset_tvkey_dongle(bus_num);
		}
			break;
		
		default:
		{
			printk(KERN_ERR "[%s] invalid ioctl num\n", __func__);
		}
			break;
	}

	return 0;
}


static const struct file_operations usben_gpio_fileops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= control_usb_power,
};


static const struct of_device_id usben_match[] = {
#if defined(CONFIG_ARCH_SDP)
	{ .compatible = "samsung,sdp-usben" },
#elif defined(CONFIG_ARCH_NVT_V7)
	{ .compatible = "samsung,nvt-usben" },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, usben_match);


#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
static int usben_get_smack64_label(struct device *dev, char* buf, int size)
{
	snprintf(buf, size, "%s", SMACK_LABEL_NAME);
	return 0;
}
#endif


int create_device_node(void)
{
	int ret = -1;

	ret = alloc_chrdev_region(&usben_id, 0, 1, "USBEN_gpio");
	if(ret < 0) {
		printk(KERN_ERR "[%s] problem in getting the major number\n", __func__);
		return ret;
	}

	cdev_init(&usben_gpio_cdev, &usben_gpio_fileops);
	ret = cdev_add(&usben_gpio_cdev, usben_id, 1);
	if(ret < 0) {
		printk(KERN_ERR "[%s] failed to add cdev\n", __func__);
		unregister_chrdev_region(usben_id, 1);
		return ret;
	}

	device_class = class_create(THIS_MODULE, "usben_class");
	if(!device_class) {
		printk(KERN_ERR "[%s] failed to create class\n", __func__);
		cdev_del(&usben_gpio_cdev);
		unregister_chrdev_region(usben_id, 1);
		return -EEXIST;
	}

#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	device_class->get_smack64_label = usben_get_smack64_label;
#endif

	if(!device_create(device_class, NULL, usben_id, NULL, DEVICE_FILE_NAME)) {
		printk(KERN_ERR "[%s] failed to create device\n", __func__);
		class_destroy(device_class);
		cdev_del(&usben_gpio_cdev);
		unregister_chrdev_region(usben_id, 1);
		return -EINVAL;
	}

	return ret;
}


void destroy_device_node(void)
{
	device_destroy(device_class, usben_id);
	class_destroy(device_class);
	cdev_del(&usben_gpio_cdev);
	unregister_chrdev_region(usben_id, 1);
}


static int usben_probe(struct platform_device *pdev)
{
	int ret = -1;

	ret = create_device_node();
	if(ret >= 0) {
		ret = init_usb_module(pdev);
		if(ret == 0) {
			set_usb_power(USB_ON);
		}
		else{
			destroy_device_node();

// To support at least the micom USB port in seret.
#ifndef CONFIG_USB_MODULE
			printk(KERN_ERR "[%s] init_usb_module failed. but enable usb for seret.\n", __func__);
			set_usb_power(USB_ON);
#endif
		}
	}
	else {
		printk(KERN_ERR "[%s] failed to create /dev/usben\n", __func__);
		return ret;
	}

	return ret;
}


static struct platform_driver usben_driver = {
	.probe	= usben_probe,
	.driver	= {
#if defined(CONFIG_ARCH_SDP)
		.name			= "sdp-usben",
#elif defined(CONFIG_ARCH_NVT_V7)
		.name			= "nvt-usben",
#endif
		.owner			= THIS_MODULE,
		.bus			= &platform_bus_type,
		.of_match_table	= of_match_ptr(usben_match),
	},
};


static int __init usben_init(void)
{
	int ret = -1;

	ret = platform_driver_register(&usben_driver);

	return ret;
}


static void __exit usben_exit(void)
{
	platform_driver_unregister(&usben_driver);
}


MODULE_ALIAS("platform:sdp-usben");
module_init(usben_init);
module_exit(usben_exit);
MODULE_LICENSE("GPL");

