#include <linux/pm.h>
#include <linux/acpi.h>
#define PARALLEL_RESET_RESUME_REMOVABLE_DEVICE /*Enabling parallel resume of usser port devices for iot like chip*/
struct usb_hub_descriptor;
struct usb_dev_state;

/* Functions local to drivers/usb/core/ */

extern int usb_create_sysfs_dev_files(struct usb_device *dev);
extern void usb_remove_sysfs_dev_files(struct usb_device *dev);
extern void usb_create_sysfs_intf_files(struct usb_interface *intf);
extern void usb_remove_sysfs_intf_files(struct usb_interface *intf);
extern int usb_create_ep_devs(struct device *parent,
				struct usb_host_endpoint *endpoint,
				struct usb_device *udev);
extern void usb_remove_ep_devs(struct usb_host_endpoint *endpoint);

extern void usb_enable_endpoint(struct usb_device *dev,
		struct usb_host_endpoint *ep, bool reset_toggle);
extern void usb_enable_interface(struct usb_device *dev,
		struct usb_interface *intf, bool reset_toggles);
extern void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr,
		bool reset_hardware);
extern void usb_disable_interface(struct usb_device *dev,
		struct usb_interface *intf, bool reset_hardware);
extern void usb_release_interface_cache(struct kref *ref);
extern void usb_disable_device(struct usb_device *dev, int skip_ep0);
extern int usb_deauthorize_device(struct usb_device *);
extern int usb_authorize_device(struct usb_device *);
extern void usb_detect_quirks(struct usb_device *udev);
extern void usb_detect_interface_quirks(struct usb_device *udev);
extern int usb_remove_device(struct usb_device *udev);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern int usb_get_bos_descriptor(struct usb_device *dev);
extern void usb_release_bos_descriptor(struct usb_device *dev);
extern char *usb_cache_string(struct usb_device *udev, int index);
extern int usb_set_configuration(struct usb_device *dev, int configuration);
extern int usb_choose_configuration(struct usb_device *udev);

static inline unsigned usb_get_max_power(struct usb_device *udev,
		struct usb_host_config *c)
{
	/* SuperSpeed power is in 8 mA units; others are in 2 mA units */
	unsigned mul = (udev->speed == USB_SPEED_SUPER ? 8 : 2);

	return c->desc.bMaxPower * mul;
}

extern void usb_kick_hub_wq(struct usb_device *dev);
extern int usb_match_one_id_intf(struct usb_device *dev,
				 struct usb_host_interface *intf,
				 const struct usb_device_id *id);
extern int usb_match_device(struct usb_device *dev,
			    const struct usb_device_id *id);
extern void usb_forced_unbind_intf(struct usb_interface *intf);
extern void usb_unbind_and_rebind_marked_interfaces(struct usb_device *udev);

extern void usb_hub_release_all_ports(struct usb_device *hdev,
		struct usb_dev_state *owner);
extern bool usb_device_is_owned(struct usb_device *udev);

extern int  usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);
extern int usb_device_supports_lpm(struct usb_device *udev);

#ifdef	CONFIG_PM

extern int usb_suspend(struct device *dev, pm_message_t msg);
extern int usb_resume(struct device *dev, pm_message_t msg);
extern int usb_resume_complete(struct device *dev);

extern int usb_port_suspend(struct usb_device *dev, pm_message_t msg);
extern int usb_port_resume(struct usb_device *dev, pm_message_t msg);

extern void usb_autosuspend_device(struct usb_device *udev);
extern int usb_autoresume_device(struct usb_device *udev);
extern int usb_remote_wakeup(struct usb_device *dev);
extern int usb_runtime_suspend(struct device *dev);
extern int usb_runtime_resume(struct device *dev);
extern int usb_runtime_idle(struct device *dev);
extern int usb_set_usb2_hardware_lpm(struct usb_device *udev, int enable);

#else

static inline int usb_port_suspend(struct usb_device *udev, pm_message_t msg)
{
	return 0;
}

static inline int usb_port_resume(struct usb_device *udev, pm_message_t msg)
{
	return 0;
}

#define usb_autosuspend_device(udev)		do {} while (0)
static inline int usb_autoresume_device(struct usb_device *udev)
{
	return 0;
}

static inline int usb_set_usb2_hardware_lpm(struct usb_device *udev, int enable)
{
	return 0;
}

#endif

extern struct bus_type usb_bus_type;
extern struct mutex usb_port_peer_mutex;
extern struct device_type usb_device_type;
extern struct device_type usb_if_device_type;
extern struct device_type usb_ep_device_type;
extern struct device_type usb_port_device_type;
extern struct usb_device_driver usb_generic_driver;

static inline int is_usb_device(const struct device *dev)
{
	return dev->type == &usb_device_type;
}

static inline int is_usb_interface(const struct device *dev)
{
	return dev->type == &usb_if_device_type;
}

static inline int is_usb_endpoint(const struct device *dev)
{
	return dev->type == &usb_ep_device_type;
}

static inline int is_usb_port(const struct device *dev)
{
	return dev->type == &usb_port_device_type;
}

/* Do the same for device drivers and interface drivers. */

static inline int is_usb_device_driver(struct device_driver *drv)
{
	return container_of(drv, struct usbdrv_wrap, driver)->
			for_devices;
}

/* for labeling diagnostics */
extern const char *usbcore_name;

/* sysfs stuff */
extern const struct attribute_group *usb_device_groups[];
extern const struct attribute_group *usb_interface_groups[];

/* usbfs stuff */
extern struct mutex usbfs_mutex;
extern struct usb_driver usbfs_driver;
extern const struct file_operations usbfs_devices_fops;
extern const struct file_operations usbdev_file_operations;
extern void usbfs_conn_disc_event(void);

extern int usb_devio_init(void);
extern void usb_devio_cleanup(void);

/*
 * Firmware specific cookie identifying a port's location. '0' == no location
 * data available
 */
typedef u32 usb_port_location_t;

/* internal notify stuff */
extern void usb_notify_add_device(struct usb_device *udev);
extern void usb_notify_remove_device(struct usb_device *udev);
extern void usb_notify_add_bus(struct usb_bus *ubus);
extern void usb_notify_remove_bus(struct usb_bus *ubus);
extern void usb_hub_adjust_deviceremovable(struct usb_device *hdev,
		struct usb_hub_descriptor *desc);

#ifdef CONFIG_ACPI
extern int usb_acpi_register(void);
extern void usb_acpi_unregister(void);
extern acpi_handle usb_get_hub_port_acpi_handle(struct usb_device *hdev,
	int port1);
#else
static inline int usb_acpi_register(void) { return 0; };
static inline void usb_acpi_unregister(void) { };
#endif

/*Below information has been added to create a framework for resuming user port devices parallely */
#if defined(CONFIG_SAMSUNG_USB_PARALLEL_RESUME)||defined(CONFIG_SAMSUNG_USB_PARALLEL_RESUME_MODULE)
extern volatile unsigned long   khubd_resume_flag;
enum resume_id{
	PARALLEL_RESUME_ID =	1,
	USERPORT_RESUME_ID =	2
};
#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
extern int resume_device_interface(struct usb_device *udev, pm_message_t msg);

/* Currently only iot device is required to reset resume so count is 1 */
/* If more devices are to be added then increase this count */
#define PARALLEL_DEVICE_COUNT 	1
#define IOT_DEVICE 	0
#define OCM_DEVICE 	1
#define ZIGBEE_IOT_VENDORID	0x10c4
#define ZIGBEE_IOT_PRODUCTID	0x0003

#define USER_PORT_FAMILY	0x03

struct userport_usb_info{
	struct usb_device	*udev[PARALLEL_DEVICE_COUNT];
	struct k_info		thread_info[PARALLEL_DEVICE_COUNT];
};

#endif
#endif
#if defined(SAMSUNG_USB_BTWIFI_RESET_WAIT)
#define ZIGBEE_IOT_VENDORID		0x10c4
#define ZIGBEE_IOT_PRODUCTID		0x0003
#define BTCOMBO_QCA_PRODUCT_ID		0x3004
#define WIFICOMBO_QCA_PRODUCT_ID	0x9378
#define HUB11_VENDOR_ID			0x05e3
#define HUB11_PRODUCT_ID		0x0608
#define HUB11_BUSNO			1
#define QCA_VENDOR_ID			0x0A5C /*QCA - WIFI*/
#define BCM_VENDOR_ID			0x0CF3 /*BCM - BT, WIFI, WIFICOMBO*/
#define BCM_IOT_VENDOR_ID		0x04E8  /*BCM BT-WIFI COMBO for IoT*/
#define BTHUB_PRODUCT_ID		0x4500 /*BTHUB*/
#define BTCOMBO_PRODUCT_ID		0x2045 /*BTCOMBO*/
#define WIFI_BCM_PRODUCT_ID		0x1022 /*WIFI-BCM*/
#define WIFI_QCA_PRODUCT_ID		0xBD1D /*WIFI-QCA*/
#define WIFICOMBO_PRODUCT_ID		0XBD27 /*WIFI-COMBO*/
#define WIFI_IOT_PRODUCT_ID		0x20A0  /*BCM WIFI-COMBO for IoT*/
#define BT_IOT_PRODUCT_ID		0x20A1  /*BCM BT-COMBO for IoT*/
#define BT_QCA9379_PRODUCT_ID		0x20A4  /* BT QCA9379 combo chip */
#define WIFI_QCA9379_PRODUCT_ID	0x20A5  /* WiFi QCA9379 combo chip */
#define WIFI_MEDIATEK_VENDOR_ID	0x0E8D  /* WiFi MEDIATEK chip */
#define WIFI_MEDIATEK_PRODUCT_ID	0x7603  /* WiFi MEDIATEK  chip */
#define WIFI_SAM_PRODUCT_ID		0x20A9  /* WiFi SAMSUNG chip */

#define PORT_RESET_WAIT_SLEEP		20
#define PORT_RESET_WAIT_TIMEOUT		6000 // temporary change utill kant stable
#define IS_HAWKP			soc_is_sdp1404()
#define IS_HAWKM			soc_is_sdp1406()
#endif

#if defined(CONFIG_USB_MODULE) && defined(CONFIG_ARCH_SDP1601) && defined(__KANTM_REV_0__)
#ifndef CONFIG_BD_CACHE_ENABLED			/* If it is not AV product... */
extern unsigned int sdp_ocm_is_internal_hub(void);
extern int sdp_ocm_reset_ext_hub(void);
#define OCM_EXT_HUB_VID         0x05E3
#define OCM_EXT_HUB_PID         0x0608
#define OCM_EXT_HUB_BUSID       5
#endif
#endif


