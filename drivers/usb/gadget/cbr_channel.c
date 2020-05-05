static int channel1 = 100;
static int channel2 = 100;
module_param(channel1, int, S_IRUGO);
module_param(channel2, int, S_IRUGO);


void cbr_timer_callback(unsigned long data)
{

	struct cbr_channel *cbr = (struct cbr_channel*) data;
	struct eth_dev *dev = (struct eth_dev*) cbr->private;
	int result;


	if(dev != NULL) {
	 if(dev->cbr != NULL) {

	cbr->token = cbr->data_size_per_slot;	
	result = mod_timer(&cbr->timer, jiffies + (HZ/cbr->num_slots_per_sec)/*msecs_to_jiffies(2000)*/);
	if(result)
                printk(KERN_ERR"Error in mod_timer..%s\n",__func__);
	}
	}
	if (netif_carrier_ok(dev->net))
        	netif_wake_queue(dev->net);
}


struct cbr_channel* cbr_setup(int channel_num, void *context)
{
	struct cbr_channel *cbr = NULL;

        /* initialization of CBR module */
        if((cbr = (struct cbr_channel*) kmalloc(sizeof(struct cbr_channel), GFP_KERNEL)) == NULL) {
                printk(KERN_ERR"allocation of memmory for cbr_engine is failed !!");
		return cbr;
        }

        memset(cbr, 0, sizeof(struct cbr_channel));
        
        printk(KERN_EMERG"setup timer !!");
	cbr->channel_num = channel_num; 
	cbr->private = (void*)context;
        setup_timer(&cbr->timer, cbr_timer_callback,(unsigned int)cbr);

	/* initialize cbr channel parameters */
	if(channel_num == 1)
		cbr->bitrate = channel1 * 1024 * 1024;	/* in Mbps */	
	else if(channel_num == 2)
		cbr->bitrate = channel2 * 1024 * 1024;	/* in Mbps */	
	cbr->num_slots_per_sec = 4;	
	cbr->slot_interval = HZ/(cbr->num_slots_per_sec);	/* in jiffies */
	cbr->data_size_per_slot = cbr->bitrate/(cbr->num_slots_per_sec * 8);	/* in Bytes */

	cbr->token = cbr->data_size_per_slot;	
        
	return cbr;
}


void cbr_cleanup (struct cbr_channel* cbr)
{

	if(cbr) {
		kfree(cbr);
	}
}










