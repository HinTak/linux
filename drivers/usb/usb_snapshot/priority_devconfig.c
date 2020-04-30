/*
 * Filename: drivers/usb/core/priority_devconfig.c
 * Developed by: 14_USB
 * Date:25th September 2014
 *
 * This file provides functions to  manage the device list of priority USB devices for an usb hub.
 */
#include <linux/completion.h>
#include "snapshot.h"
#if defined (CONFIG_ARCH_SDP1404) && defined (CONFIG_MODULES)
#include <linux/module.h> 
#include <linux/mfd/sdp_micom.h> 
#endif
#undef DEBUG

#ifdef DEBUG
        #define MSG(string, args...) \
 		        printk(KERN_EMERG "%s:%d : " string, __FUNCTION__, __LINE__, ##args)
#else
        #define MSG(string, args...)
#endif

//Added EW Debug prints
#define EW_DEBUG
#ifdef EW_DEBUG
        #define EW_MSG(string, args...) \
 		        printk(KERN_EMERG "%s:%d : " string, __FUNCTION__, __LINE__, ##args)
#else
        #define EW_MSG(string, args...)
#endif

int instant_resume_update_state_disconnected(struct usb_device* udev, usbdev_state state);
product_info * resume_device_match_id (struct usb_device *udev);
struct instant_resume_tree *create_instant_resume_tree(unsigned int busnum);
int instant_resume_traverse_tree(struct instant_resume_tree* head, struct usb_device* udev, product_info *info);
int instant_resume_update_state_connected(struct usb_device* udev, struct instant_resume_tree *head, product_info *info);
int instant_resume_print_tree(struct instant_resume_tree* head);
struct instant_resume_tree  *instant_resume_tree_swap (product_info * info,struct usb_device *udev, struct instant_resume_tree *head);
void wait_till_kernel_resume_ends(void);
void instant_iotmode_decision(void);
static inline void instant_resume_end_op(void);

#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
void instant_resume_reset_user_device(void);
static inline void remove_userport_device_list(void);
#endif
#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
extern struct userport_usb_info userport_info;
#endif
static int resume_order;
static bool do_wait = true;
extern struct usb_hub_info *hub_info;

#ifdef USB_AMBIENTMODE
extern struct completion   usb_devices_resume_completion;
#endif
/* 
 * Product Info which are to be used in case of samsung parallel resume feature
 *
 * Update below structure in case of any new device is added in tree.
 */
product_info devconfig_info[MAX_PRIORITY_DEVICES+1]  = {
	{.idVendor =  0x0A5C, .idProduct = 0xBD27, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI COMBO BCM*/
	{.idVendor =  0x0A5C, .idProduct = 0xBD1D, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI ONLY BCM*/
	{.idVendor =  0x0CF3, .idProduct = 0x1022, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI ONLY QCA*/
	{.idVendor =  0x0A5C, .idProduct = 0x4500, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BTHUB BCM*/
#if defined (CONFIG_ARCH_SDP1601) && defined (__KANTM_REV_0__)		// Keep OCL Hub only for Kant.M, For rest (kant-M2 and MUSE) remove it
	{.idVendor =  0x05E3, .idProduct = 0x0608, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* Kant M OC Hub*/
#endif
	{.idVendor =  0x0A5C, .idProduct = 0x2045, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT_COMBO BCM*/
	{.idVendor =  0x0A5C, .idProduct = 0x4502, .priority = 2, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT1 DEVICE BCM */
	{.idVendor =  0x0A5C, .idProduct = 0x4503, .priority = 3, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT2 DEVICE BCM */
	{.idVendor =  0x0A5C, .idProduct = 0x22BE, .priority = 4, .dependancy_index = 1, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL},  /* BT3 DEVICE BCM */
	{.idVendor =  0x04E8, .idProduct = 0x20A0, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI COMBO BCM FOR IOT*/
	{.idVendor =  0x04E8, .idProduct = 0x20A1, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT_COMBO BCM FOR IOT*/
	{.idVendor =  0x04E8, .idProduct = 0x20A4, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BT QCA9379 Combo chip */
	{.idVendor =  0x04E8, .idProduct = 0x20A5, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI QCA9379 Combo chip */
	{.idVendor =  0x0CF3, .idProduct = 0x3004, .priority = 1, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = BT_FAMILY, .devnode = NULL}, /* BTCOMBO_QCA_PRODUCT_ID*/
	{.idVendor =  0x0CF3, .idProduct = 0x9378, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFICOMBO_QCA_PRODUCT_ID*/
	{.idVendor =  0x0E8d, .idProduct = 0x7603, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI_MEDIA_TEK*/
	{.idVendor =  0X04E8, .idProduct = 0x20A9, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI_MT7603U_SAMSUNG_PRODUCT_ID, MOB Type */
	{.idVendor =  0X04E8, .idProduct = 0x20AC, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* WIFI_MT7603U_SAMSUNG_PRODUCT_ID, Cable Type */
    	{.idVendor =  0X04E8, .idProduct = 0x20AD, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* MediaTek WIFI/BT Combo Chip(MT7668) */
	{.idVendor =  0x0E8D, .idProduct = 0x7668, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* MediaTek WIFI/BT Combo Chip(MT7668), DEV version, will remove */
	{.idVendor =  0x04E8, .idProduct = 0x20AE, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* MT7668 MOB (Module on Board) */
	{.idVendor =  0x0000, .idProduct = 0x0000, .priority = 5, .dependancy_index = 0xFF, .is_checked = 0, .usb_family = WIFI_FAMILY, .devnode = NULL}, /* Valens VS2310 */
};


extern struct instant_resume_control instant_ctrl;

/*
 * return value:
 * -1  	=> not matched
 * 0	=> matched and no dependancy
 * 1-n	=> matched and dependancy index
 *
 */

product_info * resume_device_match_id (struct usb_device *udev) 
{
	int i;
	product_info	*status = NULL;
	
	for(i = 0; i < MAX_PRIORITY_DEVICES && (devconfig_info[i].idProduct || devconfig_info[i].idVendor); i++) {
		if(udev->descriptor.idProduct == devconfig_info[i].idProduct && \
				udev->descriptor.idVendor == devconfig_info[i].idVendor) {
                        MSG("%s device id matched..%d\n", udev->devpath, i);
			status = &devconfig_info[i];
			//status = devconfig_info[i].dependancy_index;
			udev->priority = devconfig_info[i].priority;
			udev->usb_family = devconfig_info[i].usb_family;
			devconfig_info[i].is_checked = 1;
			break;
		}
		else {
			udev->priority = 0xFF;
                        MSG("%s device id not matched..%d\n", udev->devpath, i);
		}		
	}
	return status;
}

/* Build config tree for instant resume */
struct instant_resume_tree *create_instant_resume_tree(unsigned int busnum)
{
	struct instant_resume_tree *head;

	if(instant_ctrl.active & (0x01 << (busnum-1))) {
		return instant_ctrl.instant_tree[busnum -1];
	}

     	head = kzalloc(sizeof(*head), GFP_KERNEL);
        if (!head)
                return NULL;
	
	head->state = INSTANT_STATE_INIT; 
        spin_lock_init(&head->lock);
		
        INIT_LIST_HEAD(&head->dev_list);
        head->hinfo.busnum = busnum;

        /* flags */
        head->priority = 0;
        head->snapshot = 0;
        head->hub_debounce = 0;
        head->parallel = 0;
	head->num_of_nodes = 0;
        head->priority_count = 0;
	head->will_resume = 0;
        mutex_init(&head->priority_lock);
	instant_ctrl.active |= (0x1 << (busnum-1));
	instant_ctrl.instant_tree[busnum - 1] = head;
	return head;
}

int kthreadfn(void *ptrinfo);


struct resume_devnode* instant_resume_create_node(struct usb_device *udev, int priority, struct instant_resume_tree *head)
{
	struct resume_devnode* dev;
	int i;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
        //Adding APIs
//	dev->get_status = hub_port_get_status;
//	dev->reset_resume = hub_port_reset_resume;
	dev->enumerate_device = hub_port_enumerate_device;
	dev->args.udev = udev;
	dev->args.parent_portnum = udev->portnum;
	dev->args.parent_hub = NULL;

	dev->state = USBDEV_CONNECTED;
	dev->udev  = udev;
	
	dev->priority = priority;
	dev->is_snapshot = 0;
	dev->is_debounce = 0;
	if(dev->priority == MIN_PRIORITY)
		dev->is_parallel = 0;
	else
    	dev->is_parallel = 1;

	dev->is_hub	 = (udev->maxchild ? 1 : 0);

	dev->busnum = udev->bus->busnum;

        memset(dev->devpath, 0, MAX_DEVPATH_SIZE + 1);
	strncpy(dev->devpath, udev->devpath, MAX_DEVPATH_SIZE);
	
	dev->hub = udev->hub;

	for (i = 0; i < MAX_HUB_PORTS; i++) {
		dev->child[i] = NULL;;
	}
 
        if(udev->parent != NULL){      
	        dev->parent = (udev->parent)->devnode;
                if(dev->parent != NULL){
                        dev->parent->child[udev->portnum-1] = dev;
                } 
        }

	dev->sibling = NULL;

	/* update parent child pointers */
	if(udev->parent != NULL) {
		udev->parent->child[udev->portnum-1] = udev;
		udev->parent->connected_ports |= (1 << (udev->portnum - 1));
	}
	udev->devnode = dev;	
        dev->parent_hub = usb_hub_to_struct_hub(udev->parent); 
        dev->its_portnum = udev->portnum;

	/* Updating the parent devnode of children's devnode */
	for(i = 0; i < MAX_HUB_PORTS; i++){
                if(udev->child[i] != NULL){
                        if(udev->child[i]->devnode){
                                udev->child[i]->devnode->parent = dev;
                        }
                }
        }
	
	/* Updating the children devnode of current devnode */
	for(i = 0; i < MAX_HUB_PORTS; i++){
                if(udev->child[i] != NULL){
                        dev->child[i] = udev->child[i]->devnode;
                }
        }

	dev->head = head;
	dev->vendor_id =  udev->descriptor.idVendor;
	dev->product_id = udev->descriptor.idProduct;
	dev->usb_family = udev->usb_family;
	spin_lock_init(&dev->node_lock);
	/* initialize kthread associated with it */
	dev->kthread_info.threadfn = kthreadfn;
	dev->kthread_info.kname = "instant thread";
	dev->kthread_info.data = (void*)dev;
     

	create_run_kthread(&dev->kthread_info);

	return dev;
}

int is_devconfig_tree_build(void)
{
	int i, flag = 1;
	for(i = 0; i < MAX_PRIORITY_DEVICES; i++) {
		if(devconfig_info[i].is_checked == 0) {
			flag = 0;
			break;
		}
	}
	return flag;
}
int instant_resume_print_tree(struct instant_resume_tree* head);

int is_parental_dependant(struct usb_device *parent_udev, struct usb_device *udev)
{
	unsigned int flag = 0;

	while(udev != NULL) {
		if(udev == parent_udev)
			flag = 1;
		udev = udev->parent;
	}
	return flag;
}

int instant_resume_traverse_tree(struct instant_resume_tree* head, struct usb_device* udev, product_info *info)
{
	struct resume_devnode *dev, *entry;
	struct list_head	*ptr;
	int i = 0, found;
	
	while(udev != NULL) {
		if(udev->is_scanned)
			break;
		dev = instant_resume_create_node(udev, udev->priority, head);
		/* add 1st node */
                if(dev == NULL){
                        break;
                }
            
		if(head->num_of_nodes == 0) {
                        spin_lock(&head->lock);
			list_add_tail(&dev->dev_list, &head->dev_list);
                        spin_unlock(&head->lock);
		}
		else {  /* traverse the list for correct position */
			found = 0;
			list_for_each(ptr, &head->dev_list) {
				entry = list_entry(ptr, struct resume_devnode, dev_list);
				if ((is_parental_dependant(udev, entry->udev)) || (!(is_parental_dependant(entry->udev, udev)) && (udev->priority < entry->priority))) {
                        		spin_lock(&head->lock);
				    	list_add_tail(&dev->dev_list, &entry->dev_list);
        		                spin_unlock(&head->lock);
				    	found = 1;
					break;
				}
		    	}
			if(!found) /* add in list as last member */ {
	                        spin_lock(&head->lock);
				list_add_tail(&dev->dev_list, &head->dev_list);
                        	spin_unlock(&head->lock);
			}
		}
		if(i == 0) { /* device node */
			dev->instant_enable = 1;
			dev->is_checked = 1;
			dev->skip_resume = 1;
			info=resume_device_match_id(udev);
			if(info)
				info->devnode=udev->devnode;

		}
		else if(i == 1) { /* parent device node */
			if((resume_device_match_id (udev)) == NULL)
				dev->is_instant_point = 1; 
		}
		udev->is_scanned = 1;
		head->num_of_nodes++;
		udev = udev->parent;
		i++;
	}
	/* are all devices checked ? */
	if(is_devconfig_tree_build()) {
                MSG("instant tree state : INSTANT_STATE_COLDBOOT\n");
		head->state = INSTANT_STATE_COLDBOOT; 
	}

	return i;
}

int instant_resume_update_state_disconnected(struct usb_device* udev, usbdev_state state)
{
	struct resume_devnode *dev = udev->devnode;
	struct instant_resume_tree *head = NULL;	
	if(dev == NULL) {
                MSG("not a priority device..\n");
		return 0;
	}
	head = instant_ctrl.instant_tree[dev->busnum -1];
	if(head!=NULL){
		MSG("%s device is disconnected.\n", udev->devpath);
		if(dev->priority <= MAX_PRIORITY){
			mutex_lock(&head->priority_lock);
			set_bit(dev->priority, &head->priority_count);
			mutex_unlock(&head->priority_lock);
			if(dev->usb_family == BT_FAMILY){
				set_bit(dev->priority, &instant_ctrl.family_ctrl);
			}else if(dev->usb_family == WIFI_FAMILY){
				set_bit(dev->priority, &instant_ctrl.wifi_ctrl);
			}
		}
		dev->udev = (struct usb_device*)NULL;
	 	dev->parent_hub = NULL;
		dev->state = USBDEV_DISCONNECTED; 
		if(udev->parent != NULL)
	  		udev->parent->child[udev->portnum-1] = NULL;
		if(dev->is_instant_point){
			dev->head->will_resume = 0;
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
			instant_ctrl.user_port_resume = false;
#endif
		}
	}
	else {
		MSG("%s Instant tree is empty. \n", udev->devpath);
	}
	return 0;
}
struct resume_devnode* instant_resume_find_devnode_by_id(struct instant_resume_tree *head, struct usb_device *udev)
{
        struct resume_devnode *dev = NULL, *entry;
        struct list_head *ptr;

        list_for_each(ptr, &head->dev_list) {
                entry = list_entry(ptr, struct resume_devnode, dev_list);

                if((entry->vendor_id == udev->descriptor.idVendor) && (entry->product_id == udev->descriptor.idProduct)){
                        dev = entry;
			break;
		}
        }
        return dev;
}
struct resume_devnode* instant_resume_find_devnode_using_product_info(struct instant_resume_tree *head, product_info *info)
{
	struct resume_devnode *dev = NULL, *entry;
	struct list_head *ptr;

	list_for_each(ptr, &head->dev_list) {
		entry = list_entry(ptr, struct resume_devnode, dev_list);

		if((entry->vendor_id == info->idVendor) && (entry->product_id == info->idProduct)){
			dev = entry;
			break;
		}
	}
	return dev;
}
struct resume_devnode* instant_resume_find_devnode_by_devpath(struct instant_resume_tree *head, struct usb_device *udev)
{
	struct resume_devnode *dev = NULL, *entry;
	struct list_head *ptr;

       	list_for_each(ptr, &head->dev_list) {
                entry = list_entry(ptr, struct resume_devnode, dev_list);

		if(!strcmp(entry->devpath, udev->devpath)){
			dev = entry;
			break;
		} 
        }
	return dev;
}

void instant_resume_create_or_update_node(struct usb_device* udev, struct instant_resume_tree *head, product_info *info)
{
	struct resume_devnode *dev, *parent_dev;
        int num_of_nodes,i = 0;
        struct usb_device *parent_udev;

	if (!udev || !head)
		return;

	dev = instant_resume_find_devnode_by_id(head, udev);

	if((dev == NULL)) {
                MSG("First time entry is being created for %s\n",dev_name(&udev->dev));

                        /* build config tree */
                        num_of_nodes = instant_resume_traverse_tree(head, udev, info);
                        MSG(" Num of added devices = %d\n",num_of_nodes);
                        /* print list build till now */
                        instant_resume_print_tree(head);
		        return;
	}

        MSG("%s device is Re-connected\n", udev->devpath);
	dev->udev = (struct usb_device*)udev;
	udev->devnode = dev;
	dev->state = USBDEV_CONNECTED; 
        dev->parent_hub = usb_hub_to_struct_hub(udev->parent); 
        for(i = 0; i < MAX_HUB_PORTS; i++){
                if(dev->child[i] != NULL){
                        dev->child[i]->parent_hub = usb_hub_to_struct_hub(udev);
                       
                }
        }

	/* Updating parent field */
        parent_udev = udev->parent;
        parent_dev = dev->parent;
        if ((parent_udev != NULL) && (parent_dev != NULL)) {
		parent_udev->devnode = parent_dev;
		parent_dev->udev = parent_udev;
		parent_dev->state = USBDEV_CONNECTED;
		parent_dev->parent_hub = usb_hub_to_struct_hub(parent_udev->parent);

                if ((udev != NULL) && (dev != NULL)){
			parent_udev->child[udev->portnum - 1] = udev;
			parent_dev->child[udev->portnum - 1] = dev;
		}

                parent_udev = parent_udev->parent;
                parent_dev = parent_dev->parent;
                //udev = udev->parent;		/*Commenting this line as we have an useless udev assignment here.Nowhere it is used afterwards. [DEXTER issue]*/
                dev = dev->parent;
        }

        return;
}

int instant_resume_print_tree(struct instant_resume_tree* head)
{
	struct list_head *ptr;
    	struct resume_devnode *entry;
	int i;

        MSG("print tree: num_of_nodes = %d\n", head->num_of_nodes);
    	list_for_each(ptr, &head->dev_list) {
        	entry = list_entry(ptr, struct resume_devnode, dev_list);
                MSG("%s: level =%d--> udev = %u",entry->udev->devpath, entry->udev->level ,entry->udev);
		for(i = 0; i < 16; i++ ) {	
			if((entry->udev->connected_ports & (1 << i)))
                                MSG(" : %d child --> devpath = %s ",(i+1), entry->udev->child[i]->devpath);
		}

    	}
        MSG("\n print done");
        return 0;
}
/******************************************************************************************************************************/
/* The kthreadfn is the thread function that has certain attributs and responsibilites.
   For the very first time it gets invoked by kick_khubd when a parent hub of priority device gets resumed.
   It is executed to perform either reset_resume or enumerate functionality.
   The same function is executed for Bt and Wifi device in need.
   Firstly it traverses a list of devices and then take each device one by one takes some decision based on resume order.
   In order to maintain resume order among usb devices it uses condition variable.
   resume order among BT ans its children: parent_end
   resume order among BT family and Wifi : event_end
   BT_THEN_WIFI : BT_HUB => BT_CHILDREN => WIFI
   WIFI_THEN_BT : WIFI => BT_HUB => BT_CHILDREN
   BT_WIFI_BOTH : BT AND WIFI both resumes parallely without waiting for anyone.
*/
/*******************************************************************************************************************************/
#define PARALLEL_OPERATION
int kthreadfn(void *ptrinfo)
{
	struct k_info *pinfo = ptrinfo;
	struct resume_devnode	*dev = NULL;
        int flag = 0;
        struct resume_devnode *entry;
        struct list_head *ptr;
        struct usb_device *udev;
	struct instant_resume_tree *head;
     

	do {
                
			while(wait_event_interruptible_timeout(pinfo->waitQ, pinfo->wait_condition_flag || kthread_should_stop(), \
					msecs_to_jiffies(TIMEOUT_IN_MSECS)) <= TIMEOUT_OCCURRED);
			if(kthread_should_stop()) {
                        do_exit(0);
                	}
                dev = (struct resume_devnode*) (pinfo->data);
		flag = 0;
#ifdef PARALLEL_OPERATION
                head = instant_ctrl.instant_tree[dev->busnum -1];
		if((dev->is_instant_point != 1) && (dev->kthread_info.wait_condition_flag)){
			switch (resume_order){
				case BT_THEN_WIFI:
                        		if(dev->state == USBDEV_DISCONNECTED){
                                		if(dev->parent_hub != NULL){
							if((dev->usb_family == WIFI_FAMILY) && (instant_ctrl.family_ctrl != 0)){
								wait_for_completion_timeout(&instant_ctrl.event_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));
								reinit_completion(&instant_ctrl.event_end);
							}
                                        		hub_port_enumerate_device(dev->parent_hub);
						}
                          		      	mutex_lock(&head->priority_lock);
                            	    		clear_bit(dev->priority, &head->priority_count);
                                		mutex_unlock(&head->priority_lock);
						clear_bit(dev->priority, &instant_ctrl.family_ctrl);
				
						/* Send complete event after reset completion of BT hub family */
						if((instant_ctrl.family_ctrl == 0) && (dev->usb_family == BT_FAMILY))
							complete(&instant_ctrl.event_end);                                   
                        		}else{
                                		udev = dev->udev;
                            		    	if(udev != NULL){
							if((dev->usb_family == WIFI_FAMILY) && (instant_ctrl.family_ctrl != 0)){
                                                		wait_for_completion_timeout(&instant_ctrl.event_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));
                                               		 	reinit_completion(&instant_ctrl.event_end);
                                        		}
                                       		 	resume_device_interface(udev, PMSG_RESUME);   
						}
                                		mutex_lock(&head->priority_lock); 
                                		clear_bit(dev->priority, &head->priority_count);		
                               		 	mutex_unlock(&head->priority_lock);
						clear_bit(dev->priority, &instant_ctrl.family_ctrl);

						/* Send complete event after reset completion of BT hub family */
						if((instant_ctrl.family_ctrl == 0) && (dev->usb_family == BT_FAMILY))
							complete(&instant_ctrl.event_end);
                        		}
					break;
				case WIFI_THEN_BT:
					if(dev->usb_family == BT_FAMILY){
						if((!dev->is_parallel) && (instant_ctrl.wifi_ctrl != 0)){
							wait_for_completion_timeout(&instant_ctrl.event_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));			
						}else if(dev->is_parallel){
							//All the three children of BT devices are sleeping till BT hub gets resumed.
							if(instant_ctrl.wifi_ctrl != 0){
								wait_for_completion_timeout(&instant_ctrl.event_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));								
							}
							wait_for_completion_timeout(&instant_ctrl.parent_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));
						}					
						if(dev->state == USBDEV_DISCONNECTED){
							if(dev->parent_hub != NULL){
								hub_port_enumerate_device(dev->parent_hub);
							}else{
								printk(KERN_EMERG "Instant Control Error : No Parent\n");
							}
						}else{
							udev = dev->udev;
							if(udev != NULL)
								resume_device_interface(udev, PMSG_RESUME);
						}
						clear_bit(dev->priority, &head->priority_count);
						if(!dev->is_parallel){			
							complete_all(&instant_ctrl.parent_end);
						}
					}else {
						printk(KERN_ERR "Instant Contnrol Error:Family = %d\n", dev->usb_family);
					}
					break;
				case BT_WIFI_BOTH:
					if(dev->usb_family == BT_FAMILY){
						if(dev->is_parallel)
							//All the three children of BT devices are sleeping till BT hub gets resumed.
							wait_for_completion_timeout(&instant_ctrl.parent_end, msecs_to_jiffies(EVENT_WAIT_TIMEOUT));
					}
					if(dev->state == USBDEV_DISCONNECTED){
						if(dev->parent_hub != NULL){
							hub_port_enumerate_device(dev->parent_hub);
						}else{
							printk(KERN_EMERG "Instant Control Error : No Parent\n");
						}
					}else{
						udev = dev->udev;
						if(udev != NULL)
							resume_device_interface(udev, PMSG_RESUME);
					}
					clear_bit(dev->priority, &head->priority_count);
					if(!dev->is_parallel){
						complete_all(&instant_ctrl.parent_end);
					}
                                        break;
				default:
					printk(KERN_ERR "IOT MODE NOT SUPPORTED IN PARALLEL RESUME[resume_order = %d]\n", resume_order);
				}	
                }else{
                        list_for_each(ptr, &head->dev_list) {
                        /* entry is container of list_head here */
                                entry = list_entry(ptr, struct resume_devnode, dev_list);
                                if (entry == dev) {
                                        flag = 1;
                                        continue;
                                }
                                if (flag == 0)
                                        continue;
                                else {
                                	udev = entry->udev;
					switch (resume_order) {
                                		case BT_THEN_WIFI:
							if(entry->state == USBDEV_DISCONNECTED){
                                        			if(!entry->is_parallel){    
									set_bit(entry->priority, &head->priority_count);
                                                			if(entry->parent_hub != NULL)
                                                        			hub_port_enumerate_device(entry->parent_hub);
                                                      		  	mutex_lock(&head->priority_lock);
                                                        		clear_bit(entry->priority, &head->priority_count);
                                                     		   	mutex_unlock(&head->priority_lock);
									clear_bit(entry->priority, &instant_ctrl.family_ctrl);

									/* Send complete event after reset completion of BT hub family */
									if((instant_ctrl.family_ctrl == 0) && (entry->usb_family == BT_FAMILY))
										complete(&instant_ctrl.event_end);
                                                		}else{
									set_bit(entry->priority, &head->priority_count);
                                                			entry->kthread_info.wait_condition_flag = 1;
                                                     		   	wake_up(&entry->kthread_info.waitQ);
                                                		}

                                        		}else{
                                       				if(!entry->is_parallel){ 
									set_bit(entry->priority, &head->priority_count);
                                                			if(udev != NULL)
                                                        			resume_device_interface(udev, PMSG_RESUME);
                                                      		  	mutex_lock(&head->priority_lock);
                                                        		clear_bit(entry->priority, &head->priority_count);
                                                      		  	mutex_unlock(&head->priority_lock);
									clear_bit(entry->priority,&instant_ctrl.family_ctrl);
								
									/* Send complete event after reset completion of BT hub family */
									if((instant_ctrl.family_ctrl == 0) && (entry->usb_family == BT_FAMILY))
										complete(&instant_ctrl.event_end);
                                                		}else{
									set_bit(entry->priority, &head->priority_count);
                                                			entry->kthread_info.wait_condition_flag = 1;
                                                        		wake_up(&entry->kthread_info.waitQ);
                                                		}
                                        
                                        		}
							break;
						case WIFI_THEN_BT:
							if(entry->usb_family == WIFI_FAMILY){
								set_bit(entry->priority, &head->priority_count);
								set_bit(entry->priority, &instant_ctrl.wifi_ctrl);
								if(entry->state == USBDEV_DISCONNECTED){
									if(entry->parent_hub != NULL){
										hub_port_enumerate_device(entry->parent_hub);
									}else{
										printk(KERN_EMERG "Instant Control Error : No Parent \n");
									}
								}else{
									if(udev != NULL)
										resume_device_interface(udev, PMSG_RESUME);
								}
								clear_bit(entry->priority, &head->priority_count);
								clear_bit(entry->priority, &instant_ctrl.wifi_ctrl);
								complete_all(&instant_ctrl.event_end);
							}else{
								entry->kthread_info.wait_condition_flag = 1;
								set_bit(entry->priority, &head->priority_count);
								wake_up(&entry->kthread_info.waitQ);	
							}
							break;
						case BT_WIFI_BOTH:
							set_bit(entry->priority, &head->priority_count);
							entry->kthread_info.wait_condition_flag = 1;
							wake_up(&entry->kthread_info.waitQ);
							break; 
						default:
							printk(KERN_ERR "IOT MODE NOT SUPPORTED IN PARALLEL RESUME[resume_order = %d]\n", resume_order);
					}
                                }
			}
		}             


#endif
#ifdef SERIAL_OPERATION
	head = instant_ctrl.instant_tree[dev->busnum -1];
        list_for_each(ptr, &head->dev_list) {
                /* entry is container of list_head here */
                entry = list_entry(ptr, struct resume_devnode, dev_list);
                if (entry == dev) {
                        flag = 1;
                        continue;
                }
                if (flag == 0)
                        continue;
                else {
                        if((entry->priority >= MIN_PRIORITY) && (entry->priority <= MAX_PRIORITY)){
                                udev = entry->udev;
                                if(entry->state == USBDEV_DISCONNECTED){
                               
                                        hub_port_enumerate_device(entry->parent_hub); 
                                }else{
                                        resume_device_interface(udev, PMSG_RESUME);
                                }
                        }      
                }
        }
#endif

        pinfo->wait_condition_flag = 0;
	EW_MSG(" Priority Count: %ld Busnum: %d...\n", head->priority_count,dev->busnum);
        if(head->priority_count == 0){
		clear_bit(dev->busnum, &instant_ctrl.is_defer_khubd);
		EW_MSG(" Defer Khubd Val: %ld...\n", instant_ctrl.is_defer_khubd);
		wait_till_kernel_resume_ends();
		instant_resume_end_op();
	}
        
	} while (!pinfo->wait_condition_flag && !kthread_should_stop());
        return 1;
}

/**
 * usb_instant_resume_cleanup() - cleanup function for instant resume of usb devices
 *
 * This seraches for all populated instant resume tree lists of device nodes and
 * stop each one's instant resume kernel threads, remove each device node from the list,
 * free the memory to container structure variable. Lists' head node and its container operated last.
 *
 */
void usb_instant_resume_cleanup(void)
{
        struct resume_devnode *entry;
        struct list_head *ptr=NULL, *n;
        struct instant_resume_tree *head;
        int i;
	do_wait = false;
        for(i = 0; i < MAX_INSTANT_TREES; i++)
        {
                head = instant_ctrl.instant_tree[i];
		//NULL check
                if(head != NULL)
                {
			if ( list_empty(&head->dev_list))
				continue;
			
                        list_for_each_safe(ptr,n, &head->dev_list)
                        {
                                entry = list_entry(ptr, struct resume_devnode, dev_list);
                                if(entry != NULL)
                                {
					//NULL check
                                        stop_kthread(&entry->kthread_info);	
					//NULL check
                                	if(&entry->dev_list != NULL)
	                                	list_del_init(&entry->dev_list);
                                   	//NULL check
					if(entry != NULL)
						kfree(entry);
                                }
                        }
			//NULL check
                        if(&head->dev_list != NULL)
                        	list_del_init(&head->dev_list);
			//NULL check
			if(head != NULL){
                        	kfree(head);
                        	head=NULL;
				instant_ctrl.instant_tree[i]=NULL;
			}
                }
        }
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
	/* Stopping the user port thread */
	stop_kthread(&instant_ctrl.user_port_thread_info);
#endif
#ifdef PARALLEL_RESET_RESUME_REMOVABLE_DEVICE
        stop_kthread(&userport_info.thread_info[IOT_DEVICE]);
#endif
        kfree(hub_info);
        hub_info=NULL;
        instant_ctrl.iot_func = NULL;
        memset(&instant_ctrl, 0, sizeof(struct instant_resume_control));
        return;
}

void instant_resume_busnum_tree_update(struct instant_resume_tree *head)
{
	struct list_head *ptr;
	struct resume_devnode *entry;
	if(head != NULL)
	{
		list_for_each(ptr, &head->dev_list){
			entry = list_entry(ptr, struct resume_devnode, dev_list);
			if(entry != NULL)
						entry->busnum = head->hinfo.busnum;
		}
	}
}

struct instant_resume_tree *instant_resume_tree_swap (product_info *info,struct usb_device *udev, struct instant_resume_tree *head)
{
	struct instant_resume_tree *tmp,*head_2;
	int tmp_bus;
	struct resume_devnode *devnode, *parent_dev;
	struct usb_device *usb = NULL;
    devnode = (struct resume_devnode *)info->devnode;
	
	if(devnode != NULL){
		parent_dev = devnode->parent;
		head_2 = devnode->head;
	}
	else{
		return NULL;
	}

	if(udev->bus->busnum != devnode->busnum){
		usb = parent_dev->udev;
		if(usb != NULL){
			if(usb->parent == NULL){
				usb->devnode = NULL;	
			}
		}
	}

	if(udev->bus->busnum!=devnode->busnum) {
		if(head!=NULL) {
			tmp=head;
			head=head_2;
			head_2=tmp;
			tmp_bus=head->hinfo.busnum;
			head->hinfo.busnum=udev->bus->busnum;
			head_2->hinfo.busnum=tmp_bus;
		}
		else {
			head=head_2;
			head_2=NULL;
			head->hinfo.busnum=udev->bus->busnum;
		}
	
		instant_ctrl.active &= (~(0x1 << (devnode->busnum-1)));			
		instant_ctrl.instant_tree[devnode->busnum-1]=head_2;
		instant_ctrl.active |= (0x1 << (udev->bus->busnum-1));
		instant_ctrl.instant_tree[udev->bus->busnum-1]=head;
		
		instant_resume_busnum_tree_update(head);
		instant_resume_busnum_tree_update(head_2);
	}

	return head;
}

void wait_till_kernel_resume_ends(void)
{
	if(do_wait && !wait_for_completion_timeout(&instant_ctrl.kernel_resume_end, msecs_to_jiffies(USER_DEV_TIMEOUT))) {
		EW_MSG(" kthreadfn: thread execution ends one cycle\n");
        }
#ifdef USB_AMBIENTMODE
	printk(KERN_ERR "Wait for Power management to trigger resume of all USB devices \n");
	if(do_wait && !wait_for_completion_timeout(&usb_devices_resume_completion, msecs_to_jiffies(USER_DEV_TIMEOUT))) {
		printk(KERN_ERR " TIMEOUT WHILE WAITING FOR ALL USB DEVICES RESUME START\n");
        }
	printk(KERN_ERR "Wait for Power management completed for all USB devices \n");
#endif
}

static inline void instant_resume_end_op(void)
{	
	unsigned long irq_flags;
	mutex_lock(&instant_ctrl.defer_lock);
	EW_MSG(" is_defer_khubd: %ld access_once: %d..\n",instant_ctrl.is_defer_khubd,instant_ctrl.access_once);
	if((instant_ctrl.is_defer_khubd == 0) && (instant_ctrl.access_once)){
		instant_ctrl.access_once = false;
		mutex_unlock(&instant_ctrl.defer_lock);
		reinit_completion(&instant_ctrl.event_end);
		reinit_completion(&instant_ctrl.parent_end);
		reinit_completion(&instant_ctrl.kernel_resume_end);		
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
		/* Going to reset user port devices */
		instant_resume_reset_user_device();
#endif
		clear_bit(PARALLEL_RESUME_ID, &khubd_resume_flag);
		if(khubd_resume_flag == 0){
			/* Enabling khubd to run */
			spin_lock_irqsave(&instant_ctrl.off_khubd_lock, irq_flags);
			instant_ctrl.off_khubd = false;
			EW_MSG(" wakeup_khubd called..\n");
			wake_wq();
			spin_unlock_irqrestore(&instant_ctrl.off_khubd_lock, irq_flags);
		}
		mutex_lock(&instant_ctrl.defer_lock);
	}
	mutex_unlock(&instant_ctrl.defer_lock);
}
void instant_iotmode_decision(void)
{
	int bus_count = 0;
	int usb_iotmode = MODE_ONE;

	bus_count = hweight32(instant_ctrl.active);

    //Apply workaround resume sequence WIFI_THEN_BT because resume fail BT_WIFI_BOTH
	switch(usb_iotmode){
		case MODE_ONE:
			if(bus_count == 1)
				resume_order = BT_THEN_WIFI;
			else
				resume_order = BT_WIFI_BOTH;
			break;
		case MODE_TWO:
		case MODE_THREE:
		case MODE_FOUR:
			if(bus_count == 1)
				resume_order =  WIFI_THEN_BT;
			else
				resume_order = BT_WIFI_BOTH;
			break;
		default:
			if(bus_count == 1)
				resume_order = WIFI_THEN_BT;
			else
				resume_order = BT_WIFI_BOTH;
	}
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
	remove_userport_device_list();
#endif
	
}
#ifdef PARALLEL_RESET_RESUME_USER_PORT_DEVICES
void instant_resume_reset_user_device(void)
{
	struct list_head *ptr = NULL, *n;
	struct usb_device *udev;

	/* reset-resume other usb devices */
	/* print dev path of all devices in list */
	mutex_lock(&instant_ctrl.defer_lock);
	list_for_each_safe(ptr, n, &instant_ctrl.other_dev_list) {
	/* udev is container of list_head here */			
		udev = list_entry(ptr, struct usb_device, other_dev_list);
		usb_lock_device(udev);
		resume_device_interface(udev, PMSG_RESUME);
		usb_unlock_device(udev);
		if(&udev->other_dev_list != NULL)
			list_del(&udev->other_dev_list);	    	
	}
	mutex_unlock(&instant_ctrl.defer_lock);

}

int user_port_reset_thread(void *ptrinfo)
{
	unsigned long irq_flags; 
	struct k_info *pinfo = ptrinfo;
	do {
		while(wait_event_interruptible_timeout(pinfo->waitQ, pinfo->wait_condition_flag || kthread_should_stop(), \
				msecs_to_jiffies(TIMEOUT_IN_MSECS)) <= TIMEOUT_OCCURRED);
		if(kthread_should_stop()) {
                        do_exit(0);
                }
		/* khubd put to sleep */
		instant_ctrl.off_khubd = true;
		/* user port reset */
		instant_resume_reset_user_device();
		/* waking khubd */ 
		spin_lock_irqsave(&instant_ctrl.off_khubd_lock, irq_flags);
		instant_ctrl.off_khubd = false;
		wake_wq();
		spin_unlock_irqrestore(&instant_ctrl.off_khubd_lock, irq_flags);
		
		pinfo->wait_condition_flag = 0;
	} while (!pinfo->wait_condition_flag && !kthread_should_stop());

    return 0;
}

static inline void remove_userport_device_list(void)
{
        struct list_head *us_ptr = NULL, *us_n = NULL;
        struct usb_device *us_udev = NULL;
        struct instant_resume_tree *head = NULL;
        int count = 0;

        if(instant_ctrl.active) {
		if(!(list_empty(&instant_ctrl.other_dev_list))){
			list_for_each_safe(us_ptr, us_n, &instant_ctrl.other_dev_list) {
				us_udev = list_entry(us_ptr, struct usb_device, other_dev_list);
				if(&us_udev->other_dev_list != NULL)
					list_del(&us_udev->other_dev_list);
			}
			printk(KERN_EMERG "is_defer_khubd <%lu> access_once <%d> \n", instant_ctrl.is_defer_khubd, instant_ctrl.access_once);
			printk(KERN_EMERG "off_khubd <%d>\n", instant_ctrl.off_khubd);
			for(count = 0; count < MAX_INSTANT_TREES; count++){
				head = instant_ctrl.instant_tree[count];
				if(head)
					printk(KERN_EMERG "head->priority_count <%lu> \n", head->priority_count);
			}
		}
	}
	reinit_completion(&instant_ctrl.kernel_resume_end);
	return;
}
#endif

#if defined (CONFIG_ARCH_SDP1404) && defined (CONFIG_MODULES)
static void *_resolve_symbol(char *name)
{
        void *ret = 0;
        const struct kernel_symbol *sym;

        mutex_lock(&module_mutex);
        sym = find_symbol(name, NULL, NULL, 1, true);
        mutex_unlock(&module_mutex);

        if (sym) {
                ret = (void *)sym->value;
        }

        return ret;
}

int get_tv_chip_data(char *fn_str)
{
        int val = 0;
        pfn_tztv_kf_drv_get_data fn_num;
        fn_num  = (pfn_tztv_kf_drv_get_data)_resolve_symbol(fn_str);
        if(fn_num){
                fn_num(&val);
        }
        return val;
}

void enable_wifi_reset()
{

                        enum sdp_sys_info tv_side_main_chip;
                        tv_side_main_chip = get_tv_chip_data("tztv_sys_chip_type_tv");
                        if ((  tv_side_main_chip  == SYSTEM_INFO_NOVA_UHD_7000_14_TV_SIDE ) ||
                                (tv_side_main_chip == SYSTEM_INFO_GOLF_UHD_14_TV_SIDE ) ||
                                ( tv_side_main_chip == SYSTEM_INFO_NOVA_UHD_14_TV_SIDE ) ||     //Need to check for this device
                                ( tv_side_main_chip == SYSTEM_INFO_GOLF_UHD_8500_14_TV_SIDE ))   //Need to check for this device
                        {
                                char cmd = WIFI_RESET_CONTROL, ack = WIFI_RESET_CONTROL_ACK, data[2] = {0, 0};
                                int len = 1;
                                int ret,cnt = 0;
                                data[0] = 2;

                                do
                                {
                                        ret = sdp_micom_send_cmd_ack(cmd, ack, data, len);
                                        if( ret )
                                        {
                                                msleep( 50 );
                                        }
                                        cnt++;
                                } while( ret && (cnt < 2) );
                        }




}
EXPORT_SYMBOL_GPL(enable_wifi_reset);
#endif
