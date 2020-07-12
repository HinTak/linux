#ifndef __HELLO_H
#define __HELLO_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/priority_devconfig.h>
#include <linux/kthreadapi.h>
#include "../core/hub.h"

struct usb_hub_info{
        struct wq_hub_info *wq_hub;
        spinlock_t list_lock;
};

struct wq_hub_info{
    struct usb_hub *hub;
    struct list_head hub_list;
};

extern void (*fn_ptr_perform_priority_device_operation)(struct usb_device *udev, struct instant_resume_tree *head);
extern void (*fn_ptr_invoke_instant_thread)(struct usb_hub *hub);
extern void (*fn_ptr_usb_kick_wq_list)(struct usb_hub *hub);
extern int (*fn_ptr_instant_resume_update_state_disconnected)(struct usb_device* udev, usbdev_state state);
extern void hub_port_enumerate_device(struct usb_hub *phub);
extern int resume_device_interface(struct usb_device *udev, pm_message_t msg);

extern struct userport_usb_info *ptr_userport_info; 
extern product_info *ptr_userport_devinfo[PARALLEL_DEVICE_COUNT];

void init_parallel_resume(void);
void deinit_parallel_resume(void);
void wake_wq(void);
#endif
