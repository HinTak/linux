/*
 *
 * Filename: drivers/usb/core/internal_devlist.h
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * Descriptor information of priority usb devices head file
 */
#ifndef __USBCORE_PRIORITY_DEVLIST_H
#define __USBCORE_PRIORITY_DEVLIST_H

#define IS_BTHUB_FAMILY \
        ((udev->descriptor.idVendor == 0x0a5c) && \
		((udev->descriptor.idProduct == 0x4500) \
			|| (udev->descriptor.idProduct == 0x4502) \
			|| (udev->descriptor.idProduct == 0x4503) \
			|| (udev->descriptor.idProduct == 0x22be) \
			|| (udev->descriptor.idProduct == 0x2045))) \
		|| ((udev->descriptor.idVendor == 0x04e8) && \
			((udev->descriptor.idProduct == 0x20a1) \
			|| (udev->descriptor.idProduct == 0x20a4))) \
		|| ((udev->descriptor.idVendor == 0x0cf3) && \
			(udev->descriptor.idProduct == 0x3004)) \
		|| ((udev->descriptor.idVendor == 0x0e8d) && \
			(udev->descriptor.idProduct == 0x7668)) 

#define IS_WIFI \
	((udev->descriptor.idVendor == 0x0a5c) && \
		((udev->descriptor.idProduct == 0xbd27)\
		|| (udev->descriptor.idProduct == 0xbd1d))) \
		|| ((udev->descriptor.idVendor == 0x0cf3) && \
			 ((udev->descriptor.idProduct == 0x1022) \
				|| (udev->descriptor.idProduct == 0x9378))) \
		|| ((udev->descriptor.idVendor == 0x04e8) && \
			((udev->descriptor.idProduct == 0x20a0) \
				|| (udev->descriptor.idProduct == 0x20a5) \
				|| (udev->descriptor.idProduct == 0x20a9) \
				|| (udev->descriptor.idProduct == 0x20ac) \
				|| (udev->descriptor.idProduct == 0x20ad) \
				|| (udev->descriptor.idProduct == 0x20ae))) \
		|| ((udev->descriptor.idVendor == 0x0e8d) && \
			(udev->descriptor.idProduct == 0x7603))\
		|| ((udev->descriptor.idVendor == 0x0000) && \
			(udev->descriptor.idProduct == 0x0000))

#endif
