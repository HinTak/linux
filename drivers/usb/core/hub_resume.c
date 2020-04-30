/*
 * Filename: drivers/usb/core/hub_resume.c 
 * Developed by: 14_USB 
 * Date:25th September 2014  
 * 
 * This file provides functions to  resume priority USB devices for an usb hub. 
 * USB devices are either reset-resume or enumerated depending on the case. 
 * Enumeration shall happen if the device is in disconnected state. Device in 
 * suspended state shall get its state reset to resume.
 */

//For HW Clock log
#include <trace/early.h>

extern int usb_resume_interface(struct usb_device *udev, struct usb_interface *intf, pm_message_t msg, int reset_resume);
extern int usb_resume_device(struct usb_device *udev, pm_message_t msg);

/*
 * This is basically a miniature form of hub_event()
 * Hopefully, kref of hub has been incremented inside this function
 * This code avoids the dequeue-ing of hub event list
 */
void hub_port_enumerate_device(struct usb_hub *phub)
{
	struct usb_device *hdev;
	struct usb_interface *intf;
	struct device *hub_dev;
	u16 hubstatus;
	u16 hubchange;
	int i, ret;

	hdev = phub->hdev;
	hub_dev = phub->intfdev;
	intf = to_usb_interface(hub_dev);

	dev_err(hub_dev, "state %d ports %d chg %04x evt %04x\n",
			hdev->state, hdev->maxchild,
			/* NOTE: expects max 15 ports... */
			(u16) phub->change_bits[0],
			(u16) phub->event_bits[0]);

	/* Lock the device, then check to see if we were
	 * disconnected while waiting for the lock to succeed. */
    kref_get(&phub->kref);
    usb_lock_device(hdev);
	if (unlikely(phub->disconnected))
		goto out_hdev_lock;

	/* If the hub has died, clean up after it */
	if (hdev->state == USB_STATE_NOTATTACHED) {
		phub->error = -ENODEV;
		hub_quiesce(phub, HUB_DISCONNECT);
		goto out_hdev_lock;
	}

	/* Autoresume */
	ret = usb_autopm_get_interface(intf);
	if (ret) {
		dev_dbg(hub_dev, "Can't autoresume: %d\n", ret);
		goto out_hdev_lock;
	}

	/* If this is an inactive hub, do nothing */
	if (phub->quiescing)
		goto out_autopm;

	if (phub->error) {
		dev_dbg(hub_dev, "resetting for error %d\n", phub->error);

		ret = usb_reset_device(hdev);
		if (ret) {
			dev_dbg(hub_dev, "error resetting hub: %d\n", ret);
			goto out_autopm;
		}

		phub->nerrors = 0;
		phub->error = 0;
	}

	/* deal with port status changes */
	for (i = 1; i <= hdev->maxchild; i++) {
		struct usb_port *port_dev = phub->ports[i - 1];

		if (test_bit(i, phub->event_bits)
				|| test_bit(i, phub->change_bits)
				|| test_bit(i, phub->wakeup_bits)) {
			/*
			 * The get_noresume and barrier ensure that if
			 * the port was in the process of resuming, we
			 * flush that work and keep the port active for
			 * the duration of the port_event().  However,
			 * if the port is runtime pm suspended
			 * (powered-off), we leave it in that state, run
			 * an abbreviated port_event(), and move on.
			 */
			pm_runtime_get_noresume(&port_dev->dev);
			pm_runtime_barrier(&port_dev->dev);
			usb_lock_port(port_dev);
			port_event(phub, i);
			usb_unlock_port(port_dev);
			pm_runtime_put_sync(&port_dev->dev);
		}
	}

	/* deal with hub status changes */
	if (test_and_clear_bit(0, phub->event_bits) == 0)
		;	/* do nothing */
	else if (hub_hub_status(phub, &hubstatus, &hubchange) < 0)
		dev_err(hub_dev, "get_hub_status failed\n");
	else {
		if (hubchange & HUB_CHANGE_LOCAL_POWER) {
			dev_dbg(hub_dev, "power change\n");
			clear_hub_feature(hdev, C_HUB_LOCAL_POWER);
			if (hubstatus & HUB_STATUS_LOCAL_POWER)
				/* FIXME: Is this always true? */
				phub->limited_power = 1;
			else
				phub->limited_power = 0;
		}
		if (hubchange & HUB_CHANGE_OVERCURRENT) {
			u16 status = 0;
			u16 unused;

			dev_dbg(hub_dev, "over-current change\n");
			clear_hub_feature(hdev, C_HUB_OVER_CURRENT);
			msleep(500);	/* Cool down */
			hub_power_on(phub, true);
			hub_hub_status(phub, &status, &unused);
			if (status & HUB_STATUS_OVERCURRENT)
				dev_err(hub_dev, "over-current condition\n");
		}
	}

out_autopm:
	/* Balance the usb_autopm_get_interface() above */
	usb_autopm_put_interface_no_suspend(intf);
out_hdev_lock:
	usb_unlock_device(hdev);

	/* Balance the stuff in kick_hub_wq() and allow autosuspend */
	usb_autopm_put_interface(intf);
	kref_put(&phub->kref, hub_release);
}

int resume_device_interface(struct usb_device *udev, pm_message_t msg)
{
        int                     status = 0;
        int                     i;
        struct usb_interface    *intf;
        //For HW Clock log
        char tmp_data[255];

        if (udev->state == USB_STATE_NOTATTACHED) {
                status = -ENODEV;
                goto done;
        }
        udev->can_submit = 1;


        /* Resume the device */
        if (udev->state == USB_STATE_SUSPENDED || udev->reset_resume)
                status = usb_resume_device(udev, msg);

	dev_err(&udev->dev, "end of device resume with status = %d\n",status);
    //For HW Clock log
    snprintf(tmp_data, sizeof(tmp_data), "%s end of device resume with status = %d\n", dev_driver_string(&udev->dev), status);
    trace_early_message(tmp_data);

	/* Resume the interfaces */
        if (status == 0 && udev->actconfig) {
                for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
                        intf = udev->actconfig->interface[i];
                        usb_resume_interface(udev, intf, msg,
                                        udev->reset_resume);
                }
        }
        usb_mark_last_busy(udev);

 done:
        dev_vdbg(&udev->dev, "%s: status %d\n", __func__, status);
        if (!status)
                udev->reset_resume = 0;

        return status;
}

