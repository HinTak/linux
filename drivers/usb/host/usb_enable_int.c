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
#include "usb_enable_int.h"
#include <soc/sdp/soc.h>
#include <trace/early.h>


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


int send_micom_msg(int value)
{
	unsigned char cmd = 0;
	char ack = 0, data[4] = {0, };
	int len = 0, ret = 0, cnt = 0;

	cmd = micom_msg[value].msg[0];
	ack = micom_msg[value].msg[0];

	if( cmd == 0x38 )
	{
		data[0] = micom_msg[value].msg[1];
		data[1] = micom_msg[value].msg[2];

		len = 2;
	}
/*
	// It was used for Jazz.L only.
	// This is not used but reserve for the OS upgrade.
	else if( cmd == 0x36 )
	{
		data[0] = micom_msg[value].msg[1];
		data[1] = micom_msg[value].msg[2];
		data[2] = micom_msg[value].msg[3];
		data[3] = micom_msg[value].msg[4];

		len = 4;
	}
*/
	else
	{
		printk(KERN_ERR "[%s]: invalid cmd [0x%c]\n", __func__, cmd);
		return -EINVAL;
	}

	do
	{
		ret = _ext_sdp_micom_send_cmd_ack(cmd, ack, data, len);
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
	int cnt = 0;
	int ret = -1;

	if( (soc_is_sdp1701()) && ((3 == _ext_tztv_sys_get_platform_info()) || (4 == _ext_tztv_sys_get_platform_info())) )	// Kant.M2 OCL only
	{
		do
		{
			ret = _ext_sdp_ocm_dongle_onoff(value);
			msleep( 500 );
		} while(( 0 != ret) && ((++cnt) <= OCM_MAX_CTRL_TRY));

		printk( KERN_ERR "[%s] set ocl 3g to %s\n", __func__, value == 1 ? "high" : "low" );
	}
	else	// Kant.M, Kant.M2 Built-in
	{
		send_micom_msg(value);
		printk( KERN_ERR "[%s] set 3g to %s\n", __func__, value == 1 ? "high" : "low" );
	}

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

	do
	{
		// ocm_gpio_write always retrun '0'.
		// It has to check whether it was correctly set up using ocm_gpio_read.
		_ext_sdp_ocm_gpio_write(OCM_USB_ENABLE, value);
		msleep( 500 );
	} while((value != _ext_sdp_ocm_gpio_read(OCM_USB_ENABLE)) && ((++cnt) <= OCM_MAX_CTRL_TRY));

	printk( KERN_ERR "[%s] set ocm to %s [%d]\n", __func__, value == 1 ? "high" : "low", cnt );
}

void set_OCL_port(unsigned int value)
{
	int cnt = 0;

	do
	{
		// ocl_gpio_write always retrun '0'.
		// It has to check whether it was correctly set up using ocm_gpio_read.
		_ext_sdp_ocm_gpio_write(OCL_USB_ENABLE_1, value);		// usb 2-1.2
		_ext_sdp_ocm_gpio_write(OCL_USB_ENABLE_2, value);		// usb 3-1.4
		msleep( 500 );
	} while((value != _ext_sdp_ocm_gpio_read(OCL_USB_ENABLE_1)) && (value != _ext_sdp_ocm_gpio_read(OCL_USB_ENABLE_2)) && ((++cnt) <= OCM_MAX_CTRL_TRY));

	printk( KERN_ERR "[%s] set ocl to %s [%d]\n", __func__, value == 1 ? "high" : "low", cnt );
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

/*
			====================================================
			| Year | SoC	  | Board Type | Power Control	   |
			====================================================
			| 2017 | Kant.M   | Built-in   | CPU & Micom	   |
			-	   -		  ----------------------------------
			|	   |		  | OCM 	   | CPU & Micom & I2C |		// The power control of the main board differs depending on the region.
			-	   -		  ----------------------------------
			|	   |		  | OC		   | CPU & Micom	   |
			----------------------------------------------------
			| 2018 | Kant.M2  | Built-in   | CPU & Micom	   |
			-	   -		  ----------------------------------
			|	   |		  | OCL 	   | I2C & Micom	   |
			-	   ---------------------------------------------
			|	   | Kant.M2E | Built-in   | CPU & Micom	   |
			====================================================
*/

			if(ioctl_param)			// Set USB user port to high
			{
				set_3G_port(USB_SET_HIGH);

#if defined(__KANTM_REV_0__)		// Kant.M

				if( _ext_tztv_sys_is_ocm_model() == 1 )		// OCM model
				{
					if( tztv_hdmi_isOCMConnected == OCM_PLUG )
					{
						set_OCM_port(USB_SET_HIGH);
					}
					else
					{
						printk( KERN_ERR "[%s] OCM is unplugged\n", __func__ );
					}
				}

				set_GPIO_port(USB_SET_HIGH);

#elif defined(__KANTM_REV_1__)		// Kant.M2

				if((3 == _ext_tztv_sys_get_platform_info()) || (4 == _ext_tztv_sys_get_platform_info()))		// Kant.M2 OCL
				{
					set_OCL_port(USB_SET_HIGH);
				}
				else
				{
					set_GPIO_port(USB_SET_HIGH);
				}

#endif
			}
			else if(!ioctl_param)	// Set USB user port to low
			{
				set_3G_port(USB_SET_LOW);

#if defined(__KANTM_REV_0__)		// Kant.M

				if( _ext_tztv_sys_is_ocm_model() == 1 )			// OCM model
				{
					if( tztv_hdmi_isOCMConnected == OCM_PLUG )
					{
						set_OCM_port(USB_SET_LOW);
					}
					else
					{
						printk( KERN_ERR "[%s] OCM is unplugged\n", __func__ );
					}
				}

				set_GPIO_port(USB_SET_LOW);

#elif defined(__KANTM_REV_1__)		// Kant.M2

				if((3 == _ext_tztv_sys_get_platform_info()) || (4 == _ext_tztv_sys_get_platform_info())) 	// Kant.M2 OCL
				{
					set_OCL_port(USB_SET_LOW);
				}
				else
				{
					set_GPIO_port(USB_SET_LOW);
				}

#endif
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
			int bus_num = -1, port_num = -1;

			bus_num = ioctl_param & BUS_NUM_MASK;
			port_num = (ioctl_param >> PORT_NUM_SHIFT) & PORT_NUM_MASK;
			printk(KERN_ERR "[%s] reset bus: %d, port: %d\n", __func__, bus_num, port_num);

			if(port_num != 1)			// Do not use the hub for TVKey
			{
				printk(KERN_ERR "[%s] invalid port for TVKey dongle\n", __func__);
				return -EINVAL;
			}

/*
			// It was used for Jazz.L only.
			// This is not used but reserve for the OS upgrade.

			if(bus_num != TVKEY_JAZZM_SUPPORT_USB_BUS)
			{
				printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
				return -EINVAL;
			}

			printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

			set_3G_port(USB_SET_LOW);
			msleep(TVKEY_RESET_TIME);
			set_3G_port(USB_SET_HIGH);
*/

			//==================================================//
			// TVKey support port info. 						//
			//													//
			// ** Kant.M										//
			//	-. OC		: 2-1(by micom) 					//
			//	-. OCM		: 2-1(by micom) 					//
			//	-. Built-in	: 2-1(by micom), 3-1(by gpio)		//
			//													//
			// ** Kant.M2(E)									//
			//	-. OCL		: 2-1.2								//
			//	-. Built-in	: 2-1(by micom), 3-1(by gpio)		//
			//==================================================//

#if defined(__KANTM_REV_0__)		// Kant.M

			if(_ext_tztv_sys_is_oc_support() == 1)			// OC model
			{
				if(bus_num != TVKEY_KANTM_OC_SUPPORT_USB_BUS)
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}

				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_3G_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_3G_port(USB_SET_HIGH);
			}
			else if(_ext_tztv_sys_is_ocm_model() == 1)		// OCM model
			{
				if(bus_num != TVKEY_KANTM_OCM_SUPPORT_USB_BUS)
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}

				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				set_3G_port(USB_SET_LOW);
				set_GPIO_port(USB_SET_LOW);
				msleep(TVKEY_RESET_TIME);
				set_3G_port(USB_SET_HIGH);
				set_GPIO_port(USB_SET_HIGH);
			}
			else		// Built-in model
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

#elif defined(__KANTM_REV_1__)		// Kant.M2

			if( (_ext_tztv_sys_get_platform_info() == 3) || (_ext_tztv_sys_get_platform_info() == 4)  )		// OCL model
			{
				if(bus_num != TVKEY_OCL_USB)
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}

				_ext_sdp_ocm_gpio_write(OCL_USB_ENABLE_1, USB_SET_LOW);
				msleep( TVKEY_RESET_TIME );
				_ext_sdp_ocm_gpio_write(OCL_USB_ENABLE_1, USB_SET_HIGH);
			}
			else		// Built-in model
			{
				if(!((bus_num == TVKEY_BUILTIN_USB_1) || (bus_num == TVKEY_BUILTIN_USB_2)))
				{
					printk(KERN_ERR "[%s] invalid bus for TVKey dongle\n", __func__);
					return -EINVAL;
				}

				printk(KERN_ERR "[%s] complete TVKey port reset : %d-%d\n", __func__, bus_num, port_num);

				if( bus_num == TVKEY_BUILTIN_USB_1 )
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
	{ .compatible = "samsung,sdp-usben" },
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


static int usben_probe(struct platform_device *pdev)
{
	int ret = 0;

	usben_probe_done = 0;

	if((soc_is_sdp1701()) && ((_ext_tztv_sys_get_platform_info() == 3) || (_ext_tztv_sys_get_platform_info() == 4) ))	// Kant.M2 OCL only
	{
		printk(KERN_INFO "[%s] ocl do not use usben gpio\n", __func__);
	}
	else	// Kant.M, Kant.M2 Built-in
	{
		gpio = of_get_named_gpio(pdev->dev.of_node, "samsung,usb-enable", 0);
		if(!gpio_is_valid(gpio))
		{
			printk(KERN_ERR "[%s] invalid GPIO\n", __func__);
			return -EINVAL;
		}
		current_gpio_val = 1;

		printk(KERN_INFO "of_node Name = %s\n", (pdev->dev.of_node)->name);
		printk(KERN_INFO "of_node Type = %s\n", (pdev->dev.of_node)->type);
	}

	ret = alloc_chrdev_region(&usben_id, 0, 1, "USBEN_gpio");
	if(ret < 0)
	{
		printk(KERN_ERR "[%s] problem in getting the major number\n", __func__);
		return ret;
	}

	cdev_init( &usben_gpio_cdev, &usben_gpio_fileops );
	ret = cdev_add( &usben_gpio_cdev, usben_id, 1 );
	if(ret < 0)
	{
		printk(KERN_ERR "[%s] failed to add cdev\n", __func__);
		unregister_chrdev_region(usben_id, 1);
		return ret;
	}

	//create device node
	device_class = class_create(THIS_MODULE, "usben_class");
	if(!device_class)
	{
		printk(KERN_ERR "[%s] failed to create class \n", __func__);
		cdev_del(&usben_gpio_cdev);
		unregister_chrdev_region(usben_id, 1);
		return -EEXIST;
	}

#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	device_class->get_smack64_label = usben_get_smack64_label;
#endif

	if(!device_create(device_class, NULL, usben_id, NULL, DEVICE_FILE_NAME))
	{
		printk(KERN_ERR "[%s] failed to create device \n", __func__);
		class_destroy(device_class);
		cdev_del(&usben_gpio_cdev);
		unregister_chrdev_region(usben_id, 1);
		return -EINVAL;
	}

	// Enable 3G port to high
	set_3G_port(USB_SET_HIGH);

	if((soc_is_sdp1701()) && ((_ext_tztv_sys_get_platform_info() == 3) || (_ext_tztv_sys_get_platform_info() == 4)))	// Kant.M2 OCL only
	{
        set_OCL_port(USB_SET_HIGH);
		printk(KERN_INFO "[%s] ocl do not use usben gpio\n", __func__);
	}
	else	// Kant.M, Kant.M2 Built-in
	{
		ret = gpio_request((u32)gpio, "usb_enable");
		if(ret)
		{
			if(ret != -EBUSY)
			{
				dev_err(&pdev->dev, "can not request GPIO: %d\n", ret);
			}
			device_destroy(device_class, usben_id);
			class_destroy(device_class);
			cdev_del(&usben_gpio_cdev);
			unregister_chrdev_region(usben_id, 1);
			return ret;
		}

		// Set GPIO port to high
		gpio_direction_output((u32)gpio, 0);
		set_GPIO_port(USB_SET_HIGH);
	}

	usben_probe_done = 1;

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
	if(usben_probe_done) {
		device_destroy(device_class, usben_id);
		class_destroy(device_class);
		cdev_del(&usben_gpio_cdev);
		unregister_chrdev_region(usben_id, 1);
	}
	platform_driver_unregister(&usben_driver);
}


MODULE_ALIAS("platform:sdp-usben");
module_init(usben_init);
module_exit(usben_exit);
MODULE_LICENSE("GPL");

