
/******************************************************************
* 		File : Sdp_micom_irs.c
*		Description :
*		Author : ji2yoon.jo@samsung.com		
*		Date : 2019/9/20
*******************************************************************/

//#define VERSION_STR "v0.1 20190920(first version)"
//#define VERSION_STR "v0.2 20191127(change falling edge mode to rising&falling edge mode)"
//#define VERSION_STR "v0.3 20191230(add S/W workaround for fifo status bug)"
//#define VERSION_STR "v0.4 20200128(add IOCTL for IRS reset)"
#define VERSION_STR "v0.5 20200309(Change IRS Clk 24Khz -> 48Khz)"

#define DEBUG

#include <linux/device.h>
#include <linux/cdev.h>  
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/fs.h>            
#include <linux/slab.h>  
#include <asm/uaccess.h>  


#define IRSDBG(args...) dev_dbg(args)

#define DRIVER_NAME	"sdp-mcirs"
#define DEVICE_NAME	"lirc1"


#define RRCR		0x00
#define FIFOD		0x04
#define RCVSTAT		0x0c

#define FIFO_STATUS_EMPTY		0x10
#define TIME_CONVERT_USEC 		208

#define IRS_MAX_LEN_US 			500*1000
#define TIMEOUT_COUNT_MAX		145
#define EMPTY		1
#define NOT_EMPTY	0

#define IRS_RESET_CMD       		0x20
#define IRS_RESET_BIT   			9
#define SW0_RESET					0x009c0320
#define IOREMAP_SIZE				0x30



struct mcirs_dev {	
	dev_t id;  
	struct cdev cdev;  
	struct class *class;  
	struct device *dev;  
		
	void __iomem *io_base;
	int irq;
	struct completion done;	
};

void __iomem *fifo_addr;
static u32 chunk_size;
static u32 bEmpty;
static u32 bCheck_Start_Waveform;
   
static u32 count; // test
   
    
int mcirs_open (struct inode *inode, struct file *filp)  
{  
    //memset( temp, 0, 0 );     
    return 0;  
}  
    
int mcirs_close (struct inode *inode, struct file *filp)  
{     
    return 0;  
}  
    
ssize_t mcirs_read(struct file *filp, char __user *buf, size_t size, loff_t *offset)
{  
	unsigned int buff;
	int ret = 0, written = 0;
	int timeout_count = 0;
	
	//mdelay(6);	
	
	while (written < size && ret == 0){
		
		if(readl(fifo_addr) & FIFO_STATUS_EMPTY){			
						
			while(true) {	

				/* Return last pulse */
				if(bCheck_Start_Waveform == true){							
					return -1;
				}				
				
				if(readl(fifo_addr) & FIFO_STATUS_EMPTY) {					
					mdelay(1);
					timeout_count++;		
				}
				else {							
					goto not_empty_go;					
				}
				
				/* Timeout */
				if(timeout_count == TIMEOUT_COUNT_MAX) {					
					timeout_count = 0;
					break;
				}									
			}			

			if(bEmpty == EMPTY){
				bCheck_Start_Waveform = true;
				return 1;
			}
			else if(bEmpty == NOT_EMPTY){
				bEmpty = EMPTY;				
				buff = ((readl(fifo_addr+FIFOD) & 0x07ffffff) * TIME_CONVERT_USEC) / 10;
				ret = copy_to_user((void __user *)buf+written, &buff, chunk_size);								
				return ret;									
			}
		}
		else {			
			mdelay(1);			
			not_empty_go:
			
			bEmpty = NOT_EMPTY;	
			bCheck_Start_Waveform = false;			
			buff = ((readl(fifo_addr+FIFOD) & 0x07ffffff) * TIME_CONVERT_USEC) / 10;				
			
			ret = copy_to_user((void __user *)buf+written, &buff, chunk_size);			
		
			if (!ret)
				written += chunk_size;
			else
				ret = -EFAULT;
			
		}
			
	}
    
    return ret;  
} 

long mcirs_ioctl ( struct file *filp, unsigned int cmd, unsigned long arg)  
{      
    int ret = 0;
	int buff;
	void __iomem *irs_reset_addr;	
	
	switch (cmd) {
		
		case IRS_RESET_CMD:			
			irs_reset_addr = ioremap(SW0_RESET, IOREMAP_SIZE);   			
			if (!irs_reset_addr) {									
				return -1;
			}			
			
			/* IRS Reset */
			writel(readl(irs_reset_addr) & (~(0x1<<IRS_RESET_BIT)), irs_reset_addr);	
			udelay(200);					
			writel(readl(irs_reset_addr) | (0x1<<IRS_RESET_BIT), irs_reset_addr);						
			mdelay(1);
			
			/* Read Garbage Data */			
			buff = readl(fifo_addr+FIFOD);	   			
			
			/* Rising&Falling Edge Mode */
			writel(readl(fifo_addr+RRCR) | 0x203, fifo_addr+RRCR);
			
			iounmap(irs_reset_addr);
			break;

			/**********************************/
			/*								  */
			/*			   Add CMD			  */
			/*								  */
			/**********************************/			
			
		default:
			break;
			
	}	
	
    return ret;  
}  
    
    
struct file_operations mcirs_fops =  
{  
    .owner           = THIS_MODULE,  
    .read            = mcirs_read,       
    .unlocked_ioctl  = mcirs_ioctl,      
    .open            = mcirs_open,       
    .release         = mcirs_close,    
};  

static irqreturn_t mcirs_isr(int irq, void* dev)
{
	struct mcirs_dev *irsdev = dev;

	

	return IRQ_HANDLED;
}

static char *mcirs_devnode(struct device *dev, umode_t *mode)
{
        if(mode) {
                *mode = 0666;
        }
        return NULL;
}


int sdp_mcirs_init(struct mcirs_dev *irs)  
{  
    int ret;  
	unsigned buff;
	
	/* buffer clear */
	while(!(readl(fifo_addr) & FIFO_STATUS_EMPTY)){
		buff = readl(fifo_addr+FIFOD);
	}	
	buff = readl(fifo_addr+FIFOD);
	

	/* Rising&Falling Edge Mode */
	writel(readl(irs->io_base + RRCR) | 0x203, irs->io_base + RRCR);
    
    ret = alloc_chrdev_region( &irs->id, 0, 1, DEVICE_NAME );  
    if ( ret ){  
        IRSDBG(irs->dev, "alloc_chrdev_region error %d\n", ret );  
        return ret;  
    }  
    
    cdev_init( &irs->cdev, &mcirs_fops );  
    irs->cdev.owner = THIS_MODULE;  
    
    ret = cdev_add( &irs->cdev, irs->id, 1 );  
    if (ret){  
        IRSDBG(irs->dev, "cdev_add error %d\n", ret );  
        unregister_chrdev_region( irs->id, 1 );  
        return ret;  
    }  
    
    irs->class = class_create( THIS_MODULE, DEVICE_NAME );  
    if ( IS_ERR(irs->class)){  
        ret = PTR_ERR( irs->class );  
        IRSDBG(irs->dev, "class_create error %d\n", ret );  
    
        cdev_del( &irs->cdev );  
        unregister_chrdev_region( irs->id, 1 );  
        return ret;  
    }  

	irs->class->devnode = mcirs_devnode;
    
    irs->dev = device_create( irs->class, NULL, irs->id, NULL, DEVICE_NAME );  
    if ( IS_ERR(irs->dev) ){  
        ret = PTR_ERR(irs->dev);  
        IRSDBG(irs->dev, "device_create error %d\n", ret );  
    
        class_destroy(irs->class);  
        cdev_del( &irs->cdev );  
        unregister_chrdev_region( irs->id, 1 );  
        return ret;  
    }  
    
    
    return 0;  
}  
    

static int sdp_mcirs_probe(struct platform_device *pdev){
	
	struct device *dev = &pdev->dev;
	struct mcirs_dev *irsdev = NULL;
	struct resource *res = NULL;

	int ret = -EINVAL; 


	irsdev = devm_kzalloc(&pdev->dev, sizeof(*irsdev), GFP_KERNEL);
	if (!irsdev)
		return -ENOMEM;

	init_completion(&irsdev->done);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "irs");
	irsdev->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(irsdev->io_base)) {
		return PTR_ERR(irsdev->io_base);
	}
	IRSDBG(irsdev->dev, "irsdev->io_base %p\n", irsdev->io_base);
	fifo_addr = irsdev->io_base;
	chunk_size = 4;

	irsdev->irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irsdev->irq < 0) {
		dev_err(dev, "cannot find IRQ\n");
		return -ENODEV;	
	}
    
	ret = devm_request_threaded_irq(dev, irsdev->irq, mcirs_isr,
			NULL, 0, pdev->name, irsdev);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq return %d\n", ret);
		return ret;
	}

	IRSDBG(irsdev->dev, "irsdev->irq %d\n", irsdev->irq);

	irsdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, irsdev);
	
	sdp_mcirs_init(irsdev);
	
	dev_info(&pdev->dev, "probe done.%s\n", VERSION_STR);
	
    return 0;  
	
	
	
	
}
    
	
static int sdp_mcirs_remove(struct platform_device *pdev)
{
	struct mcirs_dev *irsdev = platform_get_drvdata(pdev);	
	
	device_destroy(irsdev->class, irsdev->id );  
    class_destroy(irsdev->class);  
    cdev_del( &irsdev->cdev );  
    unregister_chrdev_region( irsdev->id, 1 );  

	return 0;
}

static const struct of_device_id sdp_mcirs_match[] = {
	{.compatible = "samsung,sdp-mc-irs"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdp_mcirs_match);

static struct platform_driver sdp_mcirs_driver = {
	.probe	= sdp_mcirs_probe,
	.remove	= sdp_mcirs_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.of_match_table = sdp_mcirs_match,
	},
};	

	
module_platform_driver(sdp_mcirs_driver);

MODULE_DESCRIPTION("SDP Micom IRS Driver");
MODULE_LICENSE("GPL");
