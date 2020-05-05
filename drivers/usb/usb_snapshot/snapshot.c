#include "snapshot.h"
#define SNAPSHOT_MAX_HCD 3
LIST_HEAD(hub_list_head);

#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
/*User port devices that want to be resumed parallely should be added in below table */
product_info userport_devinfo[PARALLEL_DEVICE_COUNT]  = {{.idVendor =  ZIGBEE_IOT_VENDORID, .idProduct = ZIGBEE_IOT_PRODUCTID, .usb_family = USER_PORT_FAMILY}}; /* IOT ZIGBEE DEVICE*/
struct userport_usb_info userport_info;
#endif

struct snapshot_hcd hcd_info[SNAPSHOT_MAX_HCD]={
						{.name = "EHCI SNAPSHOT", .udev_speed = USBDEV_HIGH_SPEED, .valid = 0},
						{.name = "OHCI SNAPSHOT", .udev_speed = USBDEV_FULL_SPEED, .valid = 0},
						{.name = "XHCI SNAPSHOT", .udev_speed = USBDEV_SUPER_SPEED, .valid = 0},
					};

#if defined(CONFIG_SAMSUNG_USB_SERDES_LOCK_CHECK)
extern product_info devconfig_info[MAX_PRIORITY_DEVICES+1];
extern unsigned int tztv_sys_is_serdes_model(void);
extern int tztv_system_serdes_lock_check(void);
#define BTID_INDEX    11
#define PRIO_2                2
#endif

#ifdef USB_AMBIENTMODE
extern int thread_fn(void *unused);
extern struct k_info usb_kthread_info;
#endif

struct usb_hub_info *hub_info;
EXPORT_SYMBOL(hub_info);
extern struct instant_resume_control instant_ctrl;
extern void kick_hub_wq(struct usb_hub *hub);

#if defined(CONFIG_SAMSUNG_USB_SERDES_LOCK_CHECK)
void usb_resume_serdes_timeout(struct usb_device *udev)
{
        int       loop_count = 0;

        if(1 == tztv_sys_is_serdes_model()) {
                do {
                        if (0 == tztv_system_serdes_lock_check()) {
                                dev_err(&udev->dev, "usb resume serdes lock success after %d ms \n", loop_count*10);
                                break;
                        } else {
                                msleep(10);
                        }
                        loop_count++;
                } while (loop_count < 300);
        } else {
                dev_err(&udev->dev, "usb_resume it is not serdes model, skip check serdes lock\n");
        }
}
#endif

inline void perform_priority_device_operation(struct usb_device *udev, struct instant_resume_tree *head)
{
        product_info    *info;
	
        /* Identifing a match for BT and Wifi device if found then go for creating parallel resume tree */
        info = resume_device_match_id(udev);
        if (info != NULL) {
                /* if bus number gets changed then take this also in account */
                if(info->devnode != NULL)
                        head = instant_resume_tree_swap(info, udev, head);

                /* Create head for first time only */
                if(head == NULL)
                        head = create_instant_resume_tree(udev->bus->busnum);

                /* Create devnode for BT and wifi */
                if(head != NULL)
                        instant_resume_create_or_update_node(udev, head, info);
        }
        return;
}

#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
int resume_userport_thread(void *udev_info)
{
	struct k_info *thread_info = udev_info;
	struct usb_device *udev = NULL;
	unsigned long irq_flags;
	
	do {
                while(wait_event_interruptible_timeout(thread_info->waitQ, thread_info->wait_condition_flag || kthread_should_stop(), \
                                msecs_to_jiffies(TIMEOUT_IN_MSECS)) <= TIMEOUT_OCCURRED);
		if(kthread_should_stop()) {
                        do_exit(0);
               	}
                thread_info = udev_info;
                if(thread_info->data){
                        udev = ((struct userport_usb_info *)(thread_info->data))->udev[IOT_DEVICE];
                        if(udev){
                                usb_lock_device(udev);
                                resume_device_interface(udev, PMSG_RESUME);
                                usb_unlock_device(udev);
                        }
                }
                thread_info->wait_condition_flag = 0;
                clear_bit(USERPORT_RESUME_ID, &khubd_resume_flag);
                if(khubd_resume_flag == 0){
			spin_lock_irqsave(&instant_ctrl.off_khubd_lock, irq_flags);
			instant_ctrl.off_khubd = false;
			wake_wq();
			spin_unlock_irqrestore(&instant_ctrl.off_khubd_lock, irq_flags);
		}
        }while (!thread_info->wait_condition_flag && !kthread_should_stop());

        return 0;
}
#endif

void usb_kick_wq_list(struct usb_hub *hub)
{
        unsigned long   irq_flags;

        if(hub_info){
                struct wq_hub_info *w_hub = NULL;
                w_hub = kzalloc(sizeof(struct wq_hub_info), GFP_ATOMIC);
                if(!w_hub){
                        dev_err(hub->intfdev, "Memory allocaton faiure for w_hub!!!\n");
                        return;
                }
                spin_lock_irqsave(&hub_info->list_lock, irq_flags);
                w_hub->hub = hub;
                //is it unnecessary ?
                hub_info->wq_hub = w_hub;
                list_add_tail(&w_hub->hub_list, &hub_list_head);
                spin_unlock_irqrestore(&hub_info->list_lock, irq_flags);
        }else {
                dev_err(hub->intfdev, "Memory allocation failure during init!!\n");
        }
        return;
}
void wake_wq(void)
{
        struct list_head *ptr1, *ptr2;
        struct wq_hub_info *w_hub;
        struct usb_hub *hub;
        unsigned long   irq_flags;

        spin_lock_irqsave(&hub_info->list_lock, irq_flags);
        list_for_each_safe(ptr1, ptr2, &hub_list_head){
                w_hub = list_entry(ptr1, struct wq_hub_info, hub_list);
                hub = w_hub->hub;
                if(hub)
                        kick_hub_wq(hub);
                list_del(&w_hub->hub_list);
                kfree(w_hub);
        }
        spin_unlock_irqrestore(&hub_info->list_lock, irq_flags);

}

static inline void invoke_instant_thread(struct usb_hub *hub)
{
        int i = 0;
        unsigned long   irq_flags;
        struct resume_devnode *dev = hub->hdev->devnode;

        if((dev != NULL) && (dev->is_instant_point)){

#if defined (CONFIG_SAMSUNG_USB_SERDES_LOCK_CHECK)
#if defined(__KANTM_REV_0__)		// Kant.M	Only, Kant M2 do not need serdes lock check
		usb_resume_serdes_timeout(hub->hdev);
#endif
#endif
                spin_lock_irqsave(&dev->node_lock, irq_flags);
                if(dev->head->will_resume){
                        for(i = 0; i < MAX_HUB_PORTS; i++){
                                if(dev->child[i] != NULL){
                                        dev->child[i]->parent_hub = hub;
                                        dev->child[i]->args.parent_hub = hub;
                                }
                        }
                        dev->head->will_resume = 0;
                        set_bit(dev->head->hinfo.busnum, &instant_ctrl.is_defer_khubd);
                        set_bit(PARALLEL_RESUME_ID, &khubd_resume_flag);
                        instant_ctrl.off_khubd = true;
                        dev->kthread_info.wait_condition_flag = 1;
                        spin_unlock_irqrestore(&dev->node_lock, irq_flags);
                        dev_warn(hub->intfdev, "Going to wake instant thread\n");
                        wake_up(&dev->kthread_info.waitQ);
                        return;
                }
                spin_unlock_irqrestore(&dev->node_lock, irq_flags);
        }
}

void init_parallel_resume(void)
{
	fn_ptr_perform_priority_device_operation=perform_priority_device_operation;
	fn_ptr_invoke_instant_thread=invoke_instant_thread;
	fn_ptr_usb_kick_wq_list=usb_kick_wq_list;
	fn_ptr_instant_resume_update_state_disconnected=instant_resume_update_state_disconnected;
	ptr_userport_info=&userport_info;
	ptr_userport_devinfo[0]=&userport_devinfo[0];
}

void deinit_parallel_resume(void)
{
	fn_ptr_perform_priority_device_operation=NULL;
	fn_ptr_invoke_instant_thread=NULL;
	fn_ptr_usb_kick_wq_list=NULL;
	fn_ptr_instant_resume_update_state_disconnected=NULL;
	ptr_userport_info=NULL;
	ptr_userport_devinfo[0]=NULL;
}

void register_snapshot_hcd(struct snapshot_hcd *r_hcd)
{
	if(r_hcd->udev_speed == USBDEV_NONE)
		return;
	
	if(!hcd_info[r_hcd->udev_speed].valid)
	{
		hcd_info[r_hcd->udev_speed].valid = 1;
		printk(KERN_EMERG "%s HCD registered in Snapshot successfully\n",r_hcd->name);
	}
}
EXPORT_SYMBOL(register_snapshot_hcd);

void deregister_snapshot_hcd(struct snapshot_hcd *r_hcd)
{
	if(r_hcd->udev_speed == USBDEV_NONE)
		return;
	
	if(hcd_info[r_hcd->udev_speed].valid)        
	{
	        hcd_info[r_hcd->udev_speed].valid = 0;
		printk(KERN_EMERG "%s HCD deregistered in Snapshot Successfully\n",r_hcd->name);
		memset(r_hcd,0,sizeof(struct snapshot_hcd));
        }
}
EXPORT_SYMBOL(deregister_snapshot_hcd);

static int parallel_resume_init(void)
{
	printk(KERN_EMERG "Init Parallel Resume\n");

	init_completion(&instant_ctrl.event_end);
	init_completion(&instant_ctrl.parent_end);
	init_completion(&instant_ctrl.kernel_resume_end);
	mutex_init(&instant_ctrl.defer_lock);
	instant_ctrl.iot_func = instant_iotmode_decision;
	hub_info = kzalloc(sizeof(struct usb_hub_info), GFP_KERNEL);
	if(!hub_info){
		printk(KERN_ERR "Memory allocaton faiure for usb_hub_info!!!\n");
	}
	spin_lock_init(&hub_info->list_lock);
	spin_lock_init(&instant_ctrl.off_khubd_lock);
	
		#if defined (CONFIG_SAMSUNG_USB_SERDES_LOCK_CHECK)
        	if(1 == tztv_sys_is_serdes_model()) {
                	devconfig_info[BTID_INDEX].priority = PRIO_2;
	                printk(KERN_EMERG "Making priority of BT device as two for serdes model !!\n");
        	}
		#endif	

		#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
		INIT_LIST_HEAD(&instant_ctrl.other_dev_list);
		instant_ctrl.user_port_thread_info.threadfn = user_port_reset_thread;
		instant_ctrl.user_port_thread_info.kname = "user_port_thread";
		instant_ctrl.user_port_thread_info.data = (void *)(&instant_ctrl);
		create_run_kthread(&instant_ctrl.user_port_thread_info);
		#endif
	
		#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
		userport_info.thread_info[IOT_DEVICE].threadfn = resume_userport_thread;
		userport_info.thread_info[IOT_DEVICE].kname = "resume_thread_userport";
		userport_info.thread_info[IOT_DEVICE].data = (void *)(&userport_info);
		create_run_kthread(&userport_info.thread_info[IOT_DEVICE]);
		#endif
	init_parallel_resume();
	#ifdef USB_AMBIENTMODE
        	printk(KERN_ERR "dpm_usb_resume thread Thread creation started \n");

	        usb_kthread_info.threadfn = thread_fn;
        	usb_kthread_info.kname = "dpm_usb_resume thread";
	        usb_kthread_info.data = NULL;

        	create_run_kthread(&usb_kthread_info);
	#endif

	return 0;
}

static void parallel_resume_exit(void)
{
	printk(KERN_EMERG "Exit Parallel Resume\n ");

	deinit_parallel_resume();
	usb_instant_resume_cleanup();
}

module_init(parallel_resume_init);
module_exit(parallel_resume_exit);
MODULE_LICENSE("GPL");
