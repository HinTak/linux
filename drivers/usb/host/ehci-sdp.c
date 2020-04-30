/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/i2c.h>
#include <soc/sdp/soc.h>
#include <linux/kthread.h>
#include <linux/platform_data/ehci-sdp.h>

extern int sdp_set_clockgating(u32 phy_addr, u32 mask, u32 value);
#ifdef CONFIG_64BIT
static u64 sdp_dma_mask = DMA_BIT_MASK(64);
#else
static u64 sdp_dma_mask = DMA_BIT_MASK(32);
#endif

#ifdef CONFIG_ARCH_SDP1601
#if defined(CONFIG_SAMSUNG_USB_PARALLEL_RESUME) \
	&& defined(CONFIG_SAMSUNG_USB_SERDES_LOCK_CHECK)
#define SDP_EHCI_CHK_SERDES
#endif

#define SDP_DEV_RUN		0
#define SDP_DEV_STANDBY		1
#define SDP_MICOM_MAX_CTRL_TRY	3

enum sdp_ehci_dev_micom_ctrl
{
	SDP_USER	= 0x0,
	SDP_WIFI_CTRL	= 0x43, 
	SDP_BT_CTRL	= 0x44, 
	SDP_HUB_CTRL	= 0x45,
};

enum sdp_ehci_dev_micom_ctrl_ack
{
	SDP_USER_ACK		= 0x0,
	SDP_WIFI_CTRL_ACK	= 0x43, 
	SDP_BT_CTRL_ACK		= 0x44, 
	SDP_HUB_CTRL_ACK	= 0x45,
};

struct sdp_ehci_mcm_cmd
{
	u32 ctrl;
	u32 ctrl_ack;
	struct list_head entry;
};

struct sdp_ehci_cmd_param
{
	u32 val;
	u32 reverse;
};

typedef int (*micom_cmd_ack)(char cmd, char ack, char *data, int len);

#endif

struct sdp_ehci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;

	int no_companion;		/* No Companion */

#ifdef CONFIG_ARCH_SDP1601
	micom_cmd_ack	mcm_ctrl_fn;
	struct list_head mcm_cmd_list;
	u32 mcm_set_val;

	struct workqueue_struct *wq;
	struct work_struct work_thr;
	struct sdp_ehci_cmd_param w_param;

	int conn_usb_arg;			/* port: 0 = BT, 1 = WIFI */
	int (*conn_usbon_fn)(int port);	
#else
	u32 owner_flag;
#endif	
};

struct ehci_priv
{
	struct sdp_ehci_hcd *sdp;
};

#define sdpehci_to_ehci(ehci)	(((struct ehci_priv *)(ehci)->priv)->sdp)

/*********** Attribute ****************************************************/
static ssize_t handshake_fails_show(struct device *dev,
		struct device_attribute *addr,
		char *buf)
{
	struct sdp_ehci_hcd *sdp_ehci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	return snprintf(buf, PAGE_SIZE, "%d\n", ehci->handshake_fail_cnt);
}
DEVICE_ATTR_RO(handshake_fails);

static int device_detect_fails_show(struct device *dev,
		struct device_attribute *addr,
		char *buf)
{
	struct sdp_ehci_hcd *sdp_ehci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	return snprintf(buf, PAGE_SIZE, "%d\n", ehci->phy_device_detect_fail);
}
DEVICE_ATTR_RO(device_detect_fails);

static int cerr_count_show(struct device *dev,
		struct device_attribute *addr,
		char *buf)
{
	struct sdp_ehci_hcd *sdp_ehci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	return snprintf(buf, PAGE_SIZE, "%d\n", ehci->cerr_cnt);
}
DEVICE_ATTR_RO(cerr_count);
/*************************************************************************/

#ifdef CONFIG_ARCH_SDP1601
static unsigned long __find_symbol(struct device *dev,char *name)
{
	struct kernel_symbol *sym = NULL;
	const int MAX_CHK = 1000;
	const int SLEEP_MS = 30;
	int cnt = 0;
	
	for( cnt = 0 ; cnt < MAX_CHK ; cnt++ )
	{
		sym = (void *)find_symbol(name, NULL, NULL, 1, true);
		if(sym) {
			return sym->value;
		}
		dev_err(dev,"don't find[%d] wait[%d]ms\n",cnt,SLEEP_MS);
		msleep(SLEEP_MS);
	}
	
	dev_err(dev,"[%s] can not find a symbol[times:%d][msec:%d]\n"
		,name,MAX_CHK,SLEEP_MS);
	return (unsigned long)NULL;
}

static int __mcm_ctrl(struct device *dev,micom_cmd_ack fn
		,const struct sdp_ehci_mcm_cmd *cmd ,u8 val)
{
	int ret = 0;
	u8 data[5] = {val,0,};
	int try_cnt = 0;
	
	do {
		dev_err(dev,"[send micom:0x%X,0x%X,0x%X]\n"
			,cmd->ctrl,cmd->ctrl_ack,val);
		ret = fn( (char)(cmd->ctrl & 0xFF)
			,(char)(cmd->ctrl_ack & 0xFF),data,5);
		if( ret != 0 ) msleep(60);
	}while( ret && (try_cnt++) < SDP_MICOM_MAX_CTRL_TRY );

	return ret;
}

static void __mcm_cmd_chk_and_send(struct sdp_ehci_hcd *sdp_ehci
		,struct sdp_ehci_mcm_cmd *mcmd ,u32 val)
{
	if( mcmd == NULL || mcmd->ctrl == SDP_USER )
		return;
	
	if( __mcm_ctrl(sdp_ehci->dev,sdp_ehci->mcm_ctrl_fn,mcmd,val) != 0 ) {
		dev_err(sdp_ehci->dev
			,"fail : micom send msg[0x%X][0x%X][0x%X]\n"
			,mcmd->ctrl,mcmd->ctrl_ack,val);
	}
}

static int __mcm_get_ctrl_fn(struct sdp_ehci_hcd *sdp_ehci)
{
	if( sdp_ehci->mcm_ctrl_fn == NULL )
	{	/* set function */
		sdp_ehci->mcm_ctrl_fn = (micom_cmd_ack)__find_symbol
				(sdp_ehci->dev,"sdp_micom_send_cmd_ack");
		if( NULL == sdp_ehci->mcm_ctrl_fn ) {
			dev_err(sdp_ehci->dev,"don't find micom fn.\n");
			return -ENOENT;
		}
	}

	return 0;
}

#ifdef SDP_EHCI_CHK_SERDES
extern unsigned int tztv_sys_is_serdes_model(void);
extern int tztv_system_serdes_lock_check(void);

static bool __serdes_check_lock(struct device *dev)
{
	const int MAX_CNT = 300;
	const int DELAY_MS = 10;
	int cnt;

	int cur_jiffies = jiffies;
	dev_dbg(dev,"start to check serdes lock\n");
	
	for( cnt = 0; cnt < MAX_CNT ; cnt++ ) {
		if (tztv_system_serdes_lock_check() == 0 ) {
			dev_info(dev,"serdes lock success after %d ms\n"
				,jiffies_to_msecs(jiffies - cur_jiffies));				
			return true;
		}
		msleep(DELAY_MS);
	}

	dev_err(dev,"serdes lock check failed!\n");
	return false;
}
#else
static bool __serdes_check_lock(struct device *dev) { return true; }
#endif	

static void sdp_ehci_cmd_dev_execute
			(struct sdp_ehci_hcd *sdp_ehci,u32 val,u32 reverse)
{
	struct sdp_ehci_mcm_cmd *mcmd;
	
	if(list_empty(&sdp_ehci->mcm_cmd_list))
		return;

	if( sdp_ehci->mcm_set_val == val )
		return;

	if( __mcm_get_ctrl_fn(sdp_ehci) )
		return;

	/* prepare serdes control */
#ifdef SDP_EHCI_CHK_SERDES
	if(sdp_ehci->conn_usbon_fn && sdp_ehci->conn_usb_arg != -1) {
		int timeout=500;

		dev_err(sdp_ehci->dev,"preparing serdes line.\n");
		__serdes_check_lock(sdp_ehci->dev);
		
		while (timeout--) {
			int ret = sdp_ehci->conn_usbon_fn
					(sdp_ehci->conn_usb_arg);
			if (!ret)
				break;
			else
				msleep(1);
		}

		if (timeout >= 0)
			dev_err(sdp_ehci->dev,
				"serdes usb block port%d prepared.\n",
				sdp_ehci->conn_usb_arg);
		else
			dev_err(sdp_ehci->dev,
				"serdes usb block port%d reset fail.\n",
				sdp_ehci->conn_usb_arg);
	} else {
		dev_err(sdp_ehci->dev,"is not on serdes line.\n");
	}
#endif
	
	if( reverse ) {
		list_for_each_entry_reverse(mcmd,&sdp_ehci->mcm_cmd_list,entry){
			__mcm_cmd_chk_and_send(sdp_ehci,mcmd,val);
 		}
	}
	else {
		list_for_each_entry(mcmd,&sdp_ehci->mcm_cmd_list,entry) {
			__mcm_cmd_chk_and_send(sdp_ehci,mcmd,val);
 		}
	}

	sdp_ehci->mcm_set_val = val;
}

static void sdp_ehci_cmd_worker(struct work_struct *work)
{
	struct sdp_ehci_hcd *sdp_ehci 
		= container_of(work, struct sdp_ehci_hcd,work_thr);
	struct sdp_ehci_cmd_param *p = &sdp_ehci->w_param;

	sdp_ehci_cmd_dev_execute(sdp_ehci,p->val,p->reverse);
}

static void sdp_ehci_cmd_dev(struct sdp_ehci_hcd *sdp_ehci
		,u32 val,u32 reverse,u32 use_wq)
{
	if( use_wq ) {
		sdp_ehci->w_param.val = val;
		sdp_ehci->w_param.reverse = reverse;
		queue_work(sdp_ehci->wq, &sdp_ehci->work_thr);
	} else {
		sdp_ehci_cmd_dev_execute(sdp_ehci,val,reverse);
	}
}

/* get platform data from device tree */
static inline int sdp_ehci_get_info
		(struct sdp_ehci_hcd *sdp_ehci,struct device_node* np)
{	
	struct property	*prop;
	const __be32 *cur;
	u32 cmd;
	
	/* get info about connected device */
	/* list : of_property_for_each_u32(np, "conn_dev", prop, cur, cmd) { */
	if (!of_property_read_u32(np, "conn_dev", &cmd)) {
		struct sdp_ehci_mcm_cmd *mcmd = devm_kzalloc(sdp_ehci->dev
				,sizeof(struct sdp_ehci_mcm_cmd),GFP_KERNEL);
		
		mcmd->ctrl = cmd;
		dev_dbg(sdp_ehci->dev,"[EHCI-conn_dev:0x%X]\n",mcmd->ctrl);
		switch(mcmd->ctrl) {
			case SDP_WIFI_CTRL :
				mcmd->ctrl_ack = SDP_WIFI_CTRL_ACK;
				break;
			case SDP_BT_CTRL :
				mcmd->ctrl_ack = SDP_BT_CTRL_ACK;
				break;
			case SDP_HUB_CTRL :
				mcmd->ctrl_ack = SDP_HUB_CTRL_ACK;
				break;
			default :
				mcmd->ctrl = SDP_USER;
				mcmd->ctrl_ack = SDP_USER_ACK;
				break;
		}

		list_add_tail(&mcmd->entry,&sdp_ehci->mcm_cmd_list);
	
#ifdef SDP_EHCI_CHK_SERDES
		if(tztv_sys_is_serdes_model()) {
			sdp_ehci->conn_usbon_fn = 
				(void*)__find_symbol(sdp_ehci->dev,
						"sdp_conn_usb_on");
			switch(cmd) {
			case SDP_WIFI_CTRL:
				sdp_ehci->conn_usb_arg = 1;
				break;
			case SDP_BT_CTRL:
				sdp_ehci->conn_usb_arg = 0;
				break;
			default:
				sdp_ehci->conn_usb_arg = -1;
				break;
			}
		}
#endif
	}

	if (!of_property_read_u32(np, "no_companion", &cmd)) {
		sdp_ehci->no_companion = cmd;
		dev_err(sdp_ehci->dev,"[EHCI-No Comp.:0x%X]\n"
					,sdp_ehci->no_companion);
	}

	return 0;
}

static int sdp_ehci_run (struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret = ehci_run(hcd);

	if( ret == 0 )
	{
		struct sdp_ehci_hcd *sdp_ehci = sdpehci_to_ehci(ehci);
		sdp_ehci_cmd_dev(sdp_ehci,SDP_DEV_RUN,0,false);		
	}
	else {
		dev_err(hcd->self.controller
			,"can't start %s\n",hcd->self.bus_name);
	}

	return ret;
}

static void sdp_ehci_relinquish_port(struct usb_hcd *hcd,int portnum)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	struct sdp_ehci_hcd *sdp_ehci = sdpehci_to_ehci(ehci);
	
	if (ehci_is_TDI(ehci))
		return;

	if( sdp_ehci != NULL && sdp_ehci->no_companion ) {
		u32 __iomem *stat_reg = &ehci->regs->port_status[portnum - 1];
		u32 val;

		spin_lock_irq(&ehci->lock);
	
		val = ehci_readl(ehci,stat_reg) & (~PORT_OWNER);
		ehci_writel(ehci,val,stat_reg);		
		ehci_err(ehci,"pass set OWNER[0x%x]\n"
				,ehci_readl(ehci,stat_reg));
		
		spin_unlock_irq(&ehci->lock);
	}
	else {
		ehci_relinquish_port(hcd,portnum);
	}
}

static int sdp_ehci_hub_control(struct usb_hcd *hcd,u16 typeReq, u16 wValue,
			u16 wIndex, char *buf, u16 wLength )
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	struct sdp_ehci_hcd *sdp_ehci = sdpehci_to_ehci(ehci);
	int ret = ehci_hub_control(hcd,typeReq,wValue,wIndex,buf,wLength);
	
	if( sdp_ehci != NULL && sdp_ehci->no_companion ) {
		u32 __iomem *stat_reg
			= &ehci->regs->port_status[(wIndex & 0xff) - 1];
		u32 port_status;
	unsigned long flags;
	
		spin_lock_irqsave (&ehci->lock, flags);

		port_status = ehci_readl(ehci,stat_reg);		
		if( (port_status & PORT_OWNER) ) {
			port_status &= (~PORT_OWNER);
			ehci_writel(ehci,port_status,stat_reg);
			ehci_err(ehci,
				"hub_ctrl[0x%X][0x%X] pass set OWNER[0x%x]\n",
				typeReq,wValue,ehci_readl(ehci,stat_reg));
	}
	
		spin_unlock_irqrestore (&ehci->lock, flags);
}

	return ret;
}
#else
static inline int sdp_ehci_run (struct usb_hcd *hcd)
{
	return ehci_run(hcd);
}
		
static inline void sdp_ehci_relinquish_port(struct usb_hcd *hcd,int portnum)
{
	ehci_relinquish_port(hcd,portnum);
}

static int sdp_ehci_hub_control(struct usb_hcd *hcd,u16 typeReq, u16 wValue,
			u16 wIndex, char *buf, u16 wLength)
{
	return ehci_hub_control(hcd,typeReq,wValue,wIndex,buf,wLength);
}
#endif



static const struct hc_driver sdp_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "SDP EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd) +
				sizeof(struct ehci_priv) /* private data */,
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2 | HCD_BH,
	.reset			= ehci_setup,

	.start			= sdp_ehci_run,

	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	.get_frame_number	= ehci_get_frame,
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,
	.hub_status_data	= ehci_hub_status_data,
 	.hub_control			= sdp_ehci_hub_control,
	
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	
	.relinquish_port		= sdp_ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static int sdp_ehci_probe(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	struct ehci_priv *priv;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
#if defined(CONFIG_ARCH_SDP)
	pdev->dev.dma_mask = &sdp_dma_mask;
#if defined(CONFIG_ARCH_PHYS_ADDR_T_64BIT)
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);
#else	
	if (!pdev->dev.coherent_dma_mask)
	    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
#endif	
#endif

	sdp_ehci = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_ehci_hcd), GFP_KERNEL);

	if (!sdp_ehci)
		return -ENOMEM;
	sdp_ehci->dev = &pdev->dev;

	/* create hcd */
	hcd = usb_create_hcd(&sdp_ehci_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
	sdp_ehci->hcd = hcd;

	/* get register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap_nocache(&pdev->dev, res->start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	/* get irq */
	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

#ifdef CONFIG_ARCH_SDP1601
	sdp_ehci->no_companion = 0;
	sdp_ehci->mcm_ctrl_fn = NULL;
	sdp_ehci->mcm_set_val = SDP_DEV_STANDBY;
	sdp_ehci->conn_usbon_fn = NULL;
	sdp_ehci->conn_usb_arg = -1;
	
	/* get info. : have connected device / companion or not */
	INIT_LIST_HEAD(&sdp_ehci->mcm_cmd_list);
	if( sdp_ehci_get_info(sdp_ehci,pdev->dev.of_node) )
	{
		dev_err(&pdev->dev, "Failed to get info,\n");
		err = -ENXIO;
		goto fail_io;
	}

	sdp_ehci->wq = create_singlethread_workqueue("sdp_ehci_wq");
	if (!sdp_ehci->wq)
	{
		dev_err(&pdev->dev,"Failed to get mem for wq\n");
		err = -ENOMEM;
		goto fail_io;
	}

	INIT_WORK(&sdp_ehci->work_thr, sdp_ehci_cmd_worker);
#else
	/* get info. : have companion or not */
	sdp_ehci->no_companion = 0;
	sdp_ehci->owner_flag = 0;
#endif

	/* make ehci handle */	
	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->has_synopsys_reset_bug = 1; /* workdaround of 2nd port-reset fail */
	priv = (struct ehci_priv *)ehci->priv;
	priv->sdp = sdp_ehci;
	
	err = usb_add_hcd(hcd,(u32)irq, 0);/* IRQF_DISABLED */
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_io;
	}

	platform_set_drvdata(pdev, sdp_ehci);
	
	device_create_file(&pdev->dev, &dev_attr_handshake_fails);
	device_create_file(&pdev->dev, &dev_attr_device_detect_fails);
	device_create_file(&pdev->dev, &dev_attr_cerr_count);

	return 0;
fail_io:
	
#ifdef CONFIG_ARCH_SDP1601
	if( sdp_ehci->wq )
		destroy_workqueue(sdp_ehci->wq);
#endif
	usb_put_hcd(hcd);
	devm_kfree(&pdev->dev,sdp_ehci);
	return err;
}

static int sdp_ehci_remove(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;

#ifdef CONFIG_ARCH_SDP1601
	flush_workqueue(sdp_ehci->wq);
	destroy_workqueue(sdp_ehci->wq);
#endif

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}

static void sdp_ehci_shutdown(struct platform_device *pdev)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int sdp_ehci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	dev_dbg(&pdev->dev, "ehci device suspend\n");
	
	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);
	
#ifdef CONFIG_ARCH_SDP1601
	/* nothing */
#else
	if(soc_is_sdp1406fhd() || soc_is_sdp1406uhd()||soc_is_jazz())
	{
		int i;
		for (i=0; i<HCS_N_PORTS(ehci->hcs_params); i++)
		{
			u32 v = ehci_readl(ehci, &ehci->regs->port_status[i]);
			v = v >> 13;
			if(v & 0x1)
			{	
				dev_dbg(&pdev->dev, "owner !!!!\n");
				sdp_ehci->owner_flag=1;
			}
		}
	}
#endif
	ehci_halt(ehci);

	spin_lock_irq(&ehci->lock);

	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irq(&ehci->lock);

	dev_err(&pdev->dev, "ehci device suspend end\n");
	
#ifdef CONFIG_ARCH_SDP1601
	sdp_ehci->mcm_set_val = SDP_DEV_STANDBY;
#endif

	return 0;
}

static int sdp_ehci_resume(struct platform_device *pdev)
{	
	struct sdp_ehci_hcd *sdp_ehci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = sdp_ehci->hcd;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 i, v;
	
	dev_dbg(&pdev->dev, "ehci device resume start\n");
	
	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);
	
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_lock_irq(&ehci->lock);

	for (i=0; i<HCS_N_PORTS(ehci->hcs_params); i++) {
		v = ehci_readl(ehci,&ehci->regs->port_status[i]) | PORT_POWER;
		ehci_writel(ehci,v,&ehci->regs->port_status[i]);
	}
	udelay(20);

	ehci_writel(ehci, INTR_MASK, &ehci->regs->intr_enable);

#ifdef CONFIG_ARCH_SDP1601
	if( ehci->owned_ports == 0 ) {
		dev_dbg(&pdev->dev,"normal : ehci owner\n");
		ehci_writel(ehci,FLAG_CF, &ehci->regs->configured_flag);
	}
	else {
		dev_err(&pdev->dev,"set companion owner\n");
		ehci_writel(ehci, 0, &ehci->regs->configured_flag);
	}
#else
	if(soc_is_sdp1406fhd() || soc_is_sdp1406uhd()|| soc_is_jazz())
	{
		if(sdp_ehci->owner_flag!=1)
		{
			dev_dbg(&pdev->dev, "owner !!!! (owner is zero)\n");
			ehci_writel(ehci,FLAG_CF,&ehci->regs->configured_flag);
		}else {
			dev_dbg(&pdev->dev, "no owner !!!!\n");
			ehci_writel(ehci, 0, &ehci->regs->configured_flag);
		}
	}else ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
#endif

	udelay(20);
	spin_unlock_irq(&ehci->lock);

	dev_err(&pdev->dev, "ehci device resume end\n");

#ifdef CONFIG_ARCH_SDP1601
	sdp_ehci_cmd_dev(sdp_ehci,SDP_DEV_RUN,0,true);	
#endif	

	return 0;

}
#endif

static const struct of_device_id sdp_ehci_match[] = {
	{ .compatible = "samsung,sdp-ehci" },
	{},
};

MODULE_DEVICE_TABLE(of, sdp_ehci_match);

static struct platform_driver sdp_ehci_driver = {
	.probe		= sdp_ehci_probe,
	.remove		= sdp_ehci_remove,
	.shutdown	= sdp_ehci_shutdown,
	.driver = {
		.name		= "sdp-ehci",
		.owner		= THIS_MODULE,
		.bus		= &platform_bus_type,
		.of_match_table = of_match_ptr(sdp_ehci_match),
	},
#ifdef CONFIG_PM
	.suspend	= sdp_ehci_suspend,
	.resume		= sdp_ehci_resume,
#endif
};

MODULE_ALIAS("platform:sdp-ehci");
