/*
 *  linux/arch/arm/plat-sdp/sdp_pcie.c
 *
 *  PCIE functions for sdp PCIE Root Complex 
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2009 Samsung Electronics.co
 *
 *  Created by tukho.kim@samsung.com
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach/pci.h>
#include <asm/delay.h>


#include "pcie_sdp.h"
#include "ebus_ind.h"
#include "ebus.h"


#include <linux/module.h>
#include <linux/wait.h>
#include <linux/semaphore.h>

#include <linux/fs.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>

#define DRIVER_NAME		"pcie_sdp"
#define DRIVER_DESC		"pcie_sdp ebus Driver"


#undef DEBUG

#ifdef DEBUG
#define DBG(format, args...)	printk("%s[%d]\t" format, __FUNCTION__, __LINE__, ##args)
#else
#define DBG(format, args...)
#endif

typedef struct  {
	// synchronize resource
		wait_queue_head_t	syncQueue;	
		int 		syncCondition;
}sdp_pcie_config_sync_t;
typedef struct   {
	// synchronize resource
		wait_queue_head_t	syncQueue;	
		int 		syncCondition;
}sdp_pcie_mem_sync_t;
typedef struct   {
	// synchronize resource
		wait_queue_head_t	syncQueue;	
		int 		syncCondition;
}sdp_pcie_dma_sync_t;


static sdp_pcie_config_sync_t *sdp_config_sync = NULL;
static sdp_pcie_mem_sync_t *sdp_mem_sync = NULL;
static sdp_pcie_dma_sync_t *sdp_dma_sync = NULL;

static ebus_rc_reg_t	*ebus_rc_reg;	/* base virt addr. of RC registers  */

static DEFINE_SPINLOCK(sdp_mem_lock);
static DEFINE_SPINLOCK(sdp_conf_lock);
static DEFINE_MUTEX(sdp_dma_mutex);
static DEFINE_MUTEX(sdp_confg_mutex);
static DEFINE_MUTEX(sdp_mem_mutex);

unsigned int	readRegister(unsigned int address)
{
	unsigned int data = 0;
	data = *(volatile unsigned int *)address;
	return data;
}
void	writeRegister(unsigned int address, unsigned int data)
{
	*(volatile unsigned int *)address = data;
}
static void* pcie_va;

#define SDP_UDL_VIRT (unsigned long)pcie_va
#define SDP_SIZE (1<<12)
#define SDP_CONFIG_PHYS 0xF1000000
#define SDP_MEM_PHYS 0xF2000000


#define MB8AC0300_XSRAM0_VIRT 0
#define MB8AC0300_XSRAM0_SIZE 0
#define MB8AC0300_XSRAM0_PHYS 0

#define MB8AC0300_XSRAM1_VIRT 0
#define MB8AC0300_XSRAM1_SIZE 0
#define MB8AC0300_XSRAM1_PHYS 0


struct resource pcie_io = {
        .name   = "PCI io apeture",
        .start  = SDP_CONFIG_PHYS,
        .end    = SDP_CONFIG_PHYS + SDP_SIZE -1,
        .flags  = IORESOURCE_IO,
};
struct resource pcie_mem = {
        .name   = "PCI MEM apeture",
        .start  = SDP_MEM_PHYS,
        .end    = SDP_MEM_PHYS + SDP_SIZE -1,
        .flags  = IORESOURCE_MEM,
};
struct resource pcie_dma = {
        .name   = "PCI MEM apeture",
    //    .start  = SDP_UDL_PHYS+0x1000000+0x1000000,
    //    .end    = SDP_UDL_PHYS + SDP_UDL_SIZE -1,
    //    .flags  = IORESOURCE_DMA,
};

#if 1
/**
 * __virt_to_phys_udl - convert the address from virtual to physical.(udl)
 * @virt_addr	virtual address
 *
 * return	0	not converted
 *		other	converted physical address
 */
static inline unsigned long __virt_to_phys_udl(void *virt_addr)
{
	if ( (SDP_UDL_VIRT <= (unsigned long)virt_addr) && ( (unsigned long)virt_addr < (SDP_UDL_VIRT +	SDP_SIZE)) )
		return ((unsigned long)virt_addr - SDP_UDL_VIRT) + SDP_MEM_PHYS;

	return 0;
}

/**
 * __virt_to_phys_sram - convert the address from virtual to physical.(SRAM)
 * @virt_addr	virtual address
 *
 * return	0	not converted
 *		other	converted physical address
 */
static inline unsigned long __virt_to_phys_sram(void *virt_addr)
{
	if ( ( MB8AC0300_XSRAM0_VIRT <= (unsigned long)virt_addr ) && ( (unsigned long)virt_addr < (MB8AC0300_XSRAM0_VIRT + MB8AC0300_XSRAM0_SIZE)) )
		return ((unsigned long)virt_addr - MB8AC0300_XSRAM0_VIRT) + MB8AC0300_XSRAM0_PHYS;

	if ( ( MB8AC0300_XSRAM1_VIRT <= (unsigned long)virt_addr ) && ( (unsigned long)virt_addr < (MB8AC0300_XSRAM1_VIRT + MB8AC0300_XSRAM1_SIZE)) )
		return ((unsigned long)virt_addr - MB8AC0300_XSRAM1_VIRT) +	MB8AC0300_XSRAM1_PHYS;

	return 0;
}
#endif

/**
 * __byte_chk - check & byte enable make.
 * @addr:	virtual address or PCI regster offset
 * @byte_data:	byte data (EBUS_BYTE:1/EBUS_WORD:2/EBUS_DWORD:4)
 * @be:		byte enable (byte enable bit)
 *
 * return	EBUS_SUCCESS	no error
 *		-EINVAL		Invalid alignment or byte data
 */
static inline int __byte_chk(unsigned long addr, unsigned long byte_data,
				unsigned char *be)
{
	switch ( byte_data ) {
		case EBUS_BYTE:
			*be = 1 << ( addr & 3 );
			break;

		case EBUS_WORD:
			if (addr & 1) return -EINVAL;
			*be = 3 << ( addr & 2 );
			break;

		case EBUS_DWORD:
			if (addr & 3) return -EINVAL;
			*be = 0xf;
			break;
		default:
			return -EINVAL;
	}
	return EBUS_SUCCESS;
}

/**
 * __get_shift - get the shift data.
 * @be:		byte enable (byte enable bit)
 * @shift_data	shift data
 *
 * return	EBUS_SUCCESS	no error
 *		-EINVAL		Invalid byte enable data.
 */
static inline int __get_shift(unsigned char be, unsigned long *shift_data)
{
	switch ( be ) {
		case 0x1:
		case 0x3:
		case 0xf:
			*shift_data = 0;
			break;
		case 0x2:
			*shift_data = 8;
			break;
		case 0x4:
		case 0xc:
			*shift_data = 16;
			break;
		case 0x8:
			*shift_data = 24;
			break;
		default:
			return -EINVAL;
	}
	return EBUS_SUCCESS;
}
/**
 * __ebus_write_mem - PCI memory write.
 * @virt_addr:	virtual address
 * @byte_data:	byte data (EBUS_BYTE:1/EBUS_WORD:2/EBUS_DWORD:4)
 * @val	:	write value
 *
 * no return value
 */
inline void __sdp_write_mem(void *virt_addr,unsigned long byte_data,unsigned long val )
{
	unsigned long flags		= 0;
	unsigned char be		= 0;
	unsigned long shift_data	= 0;
	unsigned long phy_addr		= 0;
	int ret 			= EBUS_SUCCESS;

	ret = __byte_chk( (unsigned long)virt_addr, byte_data, &be );

	if ( ret != EBUS_SUCCESS ) {
		pr_err("%s(%d): Oops. Alignment error. "
			"virt_addr=0x%lx, byte_data=0x%lx\n",
			__func__, __LINE__,
			(unsigned long)virt_addr, byte_data );
	}

	ret = __get_shift( be, &shift_data );

	if ( ret != EBUS_SUCCESS ) {
		pr_err("%s(%d): Oops. Internal error. be=0x%x\n",
						__func__, __LINE__, be );
	}

	phy_addr = __virt_to_phys_udl( virt_addr );
	
		if ( !phy_addr ) {
			panic("%s(%d): Oops. Address error. virt_addr=0x%lx\n",
					__func__, __LINE__, (unsigned long)virt_addr );
		}

	spin_lock_irqsave(&sdp_mem_lock, flags);

	sdp_mem_sync->syncCondition = 0;

	/* Destination Memory Lower Address:680h */
	ebus_rc_reg->config_io_ctl.dst_mem_low  = 	phy_addr & EBUS_ALIGN_MASK;

	/* Destination Memory Upper Address:684h */
	ebus_rc_reg->config_io_ctl.dst_mem_up   = 0;

	/* Memory TLP Header Register:688h */
	ebus_rc_reg->config_io_ctl.mem_tlp_head = EBUS_MTLPHEAD | be ;

	/* Memory TLP Transfer Write Data:68Ch */
	ebus_rc_reg->config_io_ctl.mem_tlp_write_data = val << shift_data;

	/* Memory TLP Transfer Instruction:694h */
	ebus_rc_reg->config_io_ctl.mem_tlp_inst = EBUS_MTLPINST_MTLPINSTW;

	wmb();

	spin_unlock_irqrestore(&sdp_mem_lock, flags);

	ret=wait_event_interruptible_timeout(sdp_mem_sync->syncQueue, sdp_mem_sync->syncCondition, HZ/2);
	if(ret == -ERESTARTSYS)
	{
		u64 timeout;
		timeout = jiffies + HZ;
		while(!sdp_mem_sync->syncCondition)
		{
			cond_resched();
			if(jiffies > timeout)
			{
				pr_err("%s: timeout error!!!!!\n",__func__);
				break;
			}
		}
		
	}
	else if(ret <= 0)
	{
		pr_err("%s: interrupt error!!!! ret = %d\n",__func__, ret);
	}

	return;
}
EXPORT_SYMBOL(__sdp_write_mem);

/**
 * __ebus_read_mem - PCI memory read.
 * @virt_addr:	virtual address
 * @byte_data:	byte data (EBUS_BYTE:1/EBUS_WORD:2/EBUS_DWORD:4)
 * @val	:	read value
 *
 * no return value
 */
inline void __sdp_read_mem(void *virt_addr,unsigned long byte_data,unsigned long *val )
{
	unsigned long flags		= 0;
	unsigned char be		= 0;
	unsigned long shift_data	= 0;
	unsigned long phy_addr		= 0;
	int ret 			= EBUS_SUCCESS;

	ret = __byte_chk( (unsigned long)virt_addr, byte_data, &be );

	if ( ret != EBUS_SUCCESS ) {
		pr_err("%s(%d): Oops. Alignment error. "
			"virt_addr=0x%lx, byte_data=0x%lx\n",
			__func__, __LINE__,
			(unsigned long)virt_addr, byte_data );
	}

	ret = __get_shift( be, &shift_data );

	if ( ret != EBUS_SUCCESS ) {
		pr_err("%s(%d): Oops. Internal error. be=0x%x\n",
						__func__, __LINE__, be );
	}

	phy_addr = __virt_to_phys_udl( virt_addr );

	if (!phy_addr) {
		pr_err("%s(%d): Oops. Address error. virt_addr=0x%lx\n",
				__func__, __LINE__, (unsigned long)virt_addr );
	}

	spin_lock_irqsave( &sdp_mem_lock, flags );
	
	sdp_mem_sync->syncCondition =0;

	/* Destination Memory Lower Address:680h */
	ebus_rc_reg->config_io_ctl.dst_mem_low  = 
					phy_addr & EBUS_ALIGN_MASK;

	/* Destination Memory Upper Address:684h */
	ebus_rc_reg->config_io_ctl.dst_mem_up   = 0;

	/* Memory TLP Header Register:688h */
	ebus_rc_reg->config_io_ctl.mem_tlp_head = EBUS_MTLPHEAD | be ;

	/* Memory TLP Transfer Instruction:694h */
	ebus_rc_reg->config_io_ctl.mem_tlp_inst = EBUS_MTLPINST_MTLPINSTR;

	wmb();

	spin_unlock_irqrestore(&sdp_mem_lock, flags);

	ret=wait_event_interruptible_timeout(sdp_mem_sync->syncQueue, sdp_mem_sync->syncCondition, HZ/2);
	if(ret == -ERESTARTSYS)
	{
		u64 timeout;
		timeout = jiffies + HZ;
		while(!sdp_mem_sync->syncCondition)
		{
			cond_resched();
			if(jiffies > timeout)
			{
				pr_err("%s: timeout error!!!!!\n",__func__);
				return;
				break;
			}
		}
		
	}
	else if(ret <= 0)
	{
		pr_err("%s: interrupt error!!!! ret = %d\n",__func__, ret);
		return;
	}
	/* Memory TLP Transfer Read Data:690h */
	*val = ebus_rc_reg->config_io_ctl.mem_tlp_read_data >> shift_data;
	
	return;
}
EXPORT_SYMBOL(__sdp_read_mem);

/**
 * __ebus_dmac - DMA transfer.
 * @cpu_phy_addr:	SOC physical address
 * @dev_phy_addr:	UDL physical address
 * @size	:	transfer size
 * @direction	:	direction(EBUS_DMAC_WRITE:0/EBUS_DMAC_READ:1)
 *
 * no return value
*/
static inline void __sdp_dmac(unsigned long cpu_phys_addr,unsigned long dev_phys_addr, size_t size, unsigned long direction)
{
	unsigned long  ret	= 0;
	unsigned long  ch	= 0;
	dma_ch_t *dmap		= NULL;

	wmb();

	if ( !in_interrupt() ) {
		/* process  context      (Using mutex)*/
		ch     = 0;
		mutex_lock(&sdp_dma_mutex);
	} else {
		/* interrupt context (Not using mutex) */
		ch     = 1;
	}
	sdp_dma_sync->syncCondition=0;

	dmap = &ebus_rc_reg->dma_trs.dma_ch[ch];

	/* DMA Transfer PCIe Lower Address for DMAC x ch:E00h */
	dmap->pci_low  = dev_phys_addr;

	/* DMA Transfer PCIe Upper Address for DMAC x ch:E04h */
	dmap->pci_up   = 0;

	/* DMA Transfer AXI Address for DMAC x ch:E08 */
	dmap->axi_addr = cpu_phys_addr;

	/* DMA Transfer Total Size for DMAC x ch:E0Ch*/
	dmap->size     = size;

	/* Demand DMA Transfer Setting Register for DMA x ch:E10h */
	dmap->dma_trans_set = ( EBUS_DEMANDMASET | (direction << 19) );

	/* DMAC Instruction:C00h */
	ebus_rc_reg->dma_ctl.dmac_instruction =
					( EBUS_DMACINST_TRSINSTSTART | ch );
	wmb();

	ret=wait_event_interruptible_timeout(sdp_dma_sync->syncQueue, sdp_dma_sync->syncCondition, HZ/2);
	if(ret == -ERESTARTSYS)
	{
		u64 timeout;
		timeout = jiffies + HZ;
		while(!sdp_dma_sync->syncCondition)
		{
			cond_resched();
			if(jiffies > timeout)
			{
				pr_err("%s: timeout error!!!!!\n",__func__);
				return ;
				break;
			}
		}
		
	}
	else if(ret <= 0)
	{
		pr_err("%s: interrupt error!!!! ret = %ld\n",__func__, ret);
		return ;
	}

	/* Demand DMA Transfer Status Register 1 for DMA x ch:E18h */
	while ( dmap->dma_status & EBUS_DEMANDMASTS_STS_MASK ){
#if EBUS_TIME_DMAC != 0
		udelay( EBUS_TIME_DMAC );
#endif
		barrier();
	} 

	/* Demand DMA Transfer Status Register 1 for DMA x ch:E18h */
	ret = dmap->dma_status & EBUS_DEMANDMASTS_ERR_MASK;
	if( ret ) {
		panic("%s(%d): Oops. PCI macro error. ret=%ld\n",
			__func__, __LINE__, ret );
	}

	/* DMAC Instruction:C00h */
	ebus_rc_reg->dma_ctl.dmac_instruction =	( EBUS_DMACINST_TRSINSTCLEAR | ch );

	if ( !in_interrupt() ) {
		mutex_unlock(&sdp_dma_mutex);
	}

	return;
}
EXPORT_SYMBOL(__sdp_dmac);



/**
 * __ebus_read_config - PCI configration register read.
 * @where:	PCI configration register offset
 * @byte_data:	byte data (EBUS_BYTE:1/EBUS_WORD:2/EBUS_DWORD:4)
 * @val	:	read value
 *
 * return	EBUS_SUCCESS	configration register read successful
 *		other		error code
 */

static inline int __sdp_read_config (int where, unsigned long byte_data,unsigned long *val)
{
	unsigned long flags		= 0;
	unsigned char be		= 0;
	unsigned long shift_data	= 0;
	int ret				= EBUS_SUCCESS;

	ret = __byte_chk( (unsigned long)where, byte_data, &be );

	if ( ret != EBUS_SUCCESS ) {
		return -EINVAL;
	}

	ret = __get_shift( be, &shift_data );

	if ( ret != EBUS_SUCCESS ) {
		return -EINVAL;
	}

	spin_lock_irqsave(&sdp_conf_lock, flags);
	// sync condition to false
	sdp_config_sync->syncCondition = 0;

	/* Destination Configuration ID:600h */
//	ebus_rc_reg->config_io_ctl.rsv_destid    = EBUS_DESTCONFIGID;

	/* Config Reg Number:604h */
	ebus_rc_reg->config_io_ctl.rsv_confregno = where & EBUS_ALIGN_MASK;

	/* Byte Enable:60Ch */
	ebus_rc_reg->config_io_ctl.byte_enable   = be;

	/* Config Type0 Read Instruction:644h */
	ebus_rc_reg->config_io_ctl.conf_t0_read_inst = 0;

	wmb();
	spin_unlock_irqrestore(&sdp_conf_lock, flags);

	ret=wait_event_interruptible_timeout(sdp_config_sync->syncQueue, sdp_config_sync->syncCondition, HZ/2);
	if(ret == -ERESTARTSYS)
	{
		u64 timeout;
		timeout = jiffies + HZ;
		while(!sdp_config_sync->syncCondition)
		{
			cond_resched();
			if(jiffies > timeout)
			{
				pr_err("%s: timeout error!!!!!\n",__func__);
				return ret;
				break;
			}
		}
		
	}
	else if(ret <= 0)
	{
		pr_err("%s: interrupt error!!!! ret = %d\n",__func__, ret);
		return ret;
	}
	/* Config/IO Transfer Read Data:614h */
		*val = ebus_rc_reg->config_io_ctl.trans_read_data >> shift_data;
	/* Config I/O Transfer Interrupt Clear Instruction:658h */
	ebus_rc_reg->config_io_ctl.confio_intclr_inst = 0;
	return ret;
}
/**
 * __ebus_write_config - PCI configration register write.
 * @where:	PCI configration register offset
 * @byte_data:	byte data (EBUS_BYTE:1/EBUS_WORD:2/EBUS_DWORD:4)
 * @val	:	write value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
 */

static inline int __sdp_write_config (int where, unsigned long byte_data,	unsigned long val)
{
	unsigned long flags		= 0;
	unsigned char be		= 0;
	unsigned long shift_data	= 0;
	int ret				= EBUS_SUCCESS;
	
	ret = __byte_chk( (unsigned long)where, byte_data, &be );

	if ( ret != EBUS_SUCCESS ) {
		return -EINVAL;
	}

	ret = __get_shift( be, &shift_data );

	if ( ret != EBUS_SUCCESS ) {
		return -EINVAL;
	}

	spin_lock_irqsave( &sdp_conf_lock, flags );

	sdp_config_sync->syncCondition=0;

	/* Destination Configuration ID:600h */
//	ebus_rc_reg->config_io_ctl.rsv_destid	 = EBUS_DESTCONFIGID;

	/* Config Reg Number:604h */
	ebus_rc_reg->config_io_ctl.rsv_confregno = where & EBUS_ALIGN_MASK;

	/* Byte Enable:60Ch */
	ebus_rc_reg->config_io_ctl.byte_enable	 = be;

	/* Config Type0 Write Instruction:640h */
	ebus_rc_reg->config_io_ctl.conf_t0_write_inst = val << shift_data;

	wmb();

	spin_unlock_irqrestore(&sdp_conf_lock, flags);

	ret=wait_event_interruptible_timeout(sdp_config_sync->syncQueue, sdp_config_sync->syncCondition, HZ/2);
	if(ret == -ERESTARTSYS)
	{
		u64 timeout;
		timeout = jiffies + HZ;
		while(!sdp_config_sync->syncCondition)
		{
			cond_resched();
			if(jiffies > timeout)
			{
				pr_err("%s: timeout error!!!!!\n",__func__);
				return ret;
				break;
			}
		}
		
	}
	else if(ret <= 0)
	{
		pr_err("%s: interrupt error!!!! ret = %d\n",__func__, ret);
		return ret;
	}
	/* Config I/O Transfer Interrupt Clear Instruction:658h */
	ebus_rc_reg->config_io_ctl.confio_intclr_inst = 0;

	return ret;
}

/**
 * ebus_config_init - config init end-point.
 *
 * no return value
 */
void sdp_config_init(void)
{
	int  ret	= EBUS_SUCCESS;
	u16  val	= 0;

	/* Power Management Control / Status:044h */
	ret = sdp_read_config_word( EBUS_REG_PMSTCTL, &val );

	val |= EBUS_PMSTCTL;

	ret = sdp_write_config_word( EBUS_REG_PMSTCTL, val );

	/* Base Address Register:014h */
	ret = sdp_write_config_dword( EBUS_REG_BASEADDR1, EBUS_BASEADDR1 );

	/* Command Register:004h */
	ret = sdp_read_config_word( EBUS_REG_COMMAND, &val);

	val |= EBUS_COMMAND;

	ret = sdp_write_config_word( EBUS_REG_COMMAND, val );
}
EXPORT_SYMBOL(sdp_config_init);

/**
 * ebus_write_config_byte - PCI configration register write.
 * @where:	PCI configration register offset
 * @val	:	write value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
 */
int sdp_write_config_byte(unsigned int where, u8 val)
{
	int ret	= EBUS_SUCCESS;

	ret = __sdp_write_config( where, EBUS_BYTE, (unsigned long)val );

	return ret;
}
EXPORT_SYMBOL(sdp_write_config_byte);

/**
 * ebus_write_config_word - PCI configration register write.
 * @where:	PCI configration register offset
 * @val	:	write value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
*/
int sdp_write_config_word(unsigned int where, u16 val)
{
	int ret	= EBUS_SUCCESS;

	ret = __sdp_write_config( where, EBUS_WORD, (unsigned long)val );

	return ret;
}
EXPORT_SYMBOL(sdp_write_config_word);

/**
 * ebus_write_config_dword - PCI configration register write.
 * @where:	PCI configration register offset
 * @val	:	write value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
 */
int sdp_write_config_dword(unsigned int where, u32 val)
{
	int ret	= EBUS_SUCCESS;

	ret = __sdp_write_config( where, EBUS_DWORD, (unsigned long)val );

	return ret;
}
EXPORT_SYMBOL(sdp_write_config_dword);

/**
 * ebus_write_config_byte - PCI configration register read.
 * @where:	PCI configration register offset
 * @val	:	read value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
 */
int sdp_read_config_byte( unsigned int where,u8 *val )
{
	int ret			= EBUS_SUCCESS;
	unsigned long data	= 0;

	ret = __sdp_read_config( where, EBUS_BYTE, &data );
	*val = (u8)data;

	return ret;
}
EXPORT_SYMBOL(sdp_read_config_byte);

/**
 * ebus_write_config_word - PCI configration register read.
 * @where:	PCI configration register offset
 * @val	:	read value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
*/
int sdp_read_config_word( unsigned int where, u16 *val )
{
	int ret			= EBUS_SUCCESS;
	unsigned long data	= 0;

	ret = __sdp_read_config( where, EBUS_WORD, &data );
	*val = (u16)data;

	return ret;
}
EXPORT_SYMBOL(sdp_read_config_word);

/**
 * ebus_read_config_word - PCI configration register read.
 * @where:	PCI configration register offset
 * @val	:	read value
 *
 * return	EBUS_SUCCESS	configration register write successful
 *		other		error code
*/
int sdp_read_config_dword(unsigned int where, u32 *val)
{
	int ret			= EBUS_SUCCESS;
	unsigned long data	= 0;

	ret = __sdp_read_config( where, EBUS_DWORD, &data );
	*val = (u32)data;

	return ret;
}
EXPORT_SYMBOL(sdp_read_config_dword);

/**
 * ebus_multi_write - DMA transfer.
 * @cpu_addr:	SOC virtual address
 * @dev_addr:	PCI virtual address
 * @size:	transfer size
 *
 * no return value
 */
void sdp_multi_write(void *cpu_addr, void *dev_addr, size_t size)
{
	unsigned long cpu_phys_addr	= 0;
	unsigned long dev_phys_addr	= 0;

	/* cpu_addr 1st Step : SRAM area ? */
	/* cpu_addr 2nd Step : DMA buffer area (DDR3) ? */

	cpu_phys_addr = __virt_to_phys_sram( cpu_addr );

	if ( !cpu_phys_addr ) {
		cpu_phys_addr = virt_to_phys( cpu_addr );
	}

	/* dev_addr : UDL area */

	dev_phys_addr = __virt_to_phys_udl( dev_addr );

	if ( !dev_phys_addr ) {
		pr_err("%s(%d): Oops. Address error. dev_addr=0x%lx\n",
			__func__, __LINE__, (unsigned long)dev_addr );
	}

	__sdp_dmac( cpu_phys_addr, dev_phys_addr, size, EBUS_DMAC_WRITE );

	return;
}
EXPORT_SYMBOL(sdp_multi_write);

/**
 * ebus_multi_read - DMA transfer.
 * @cpu_addr:	SOC virtual address
 * @dev_addr:	PCI virtual address
 * @size:	transfer size
 *
 * no return value
 */
void sdp_multi_read(void *cpu_addr, void *dev_addr, size_t size)
{
	unsigned long cpu_phys_addr	= 0;
	unsigned long dev_phys_addr	= 0;

	/* cpu_addr 1st Step : SRAM area ? */
	/* cpu_addr 2nd Step : DMA buffer area (DDR3) ? */

	cpu_phys_addr = __virt_to_phys_sram( cpu_addr );

	if ( !cpu_phys_addr ) {
		cpu_phys_addr = virt_to_phys( cpu_addr );
	}

	/* dev_addr : UDL area */

	dev_phys_addr = __virt_to_phys_udl( dev_addr );

	if ( !dev_phys_addr ) {
		pr_err("%s(%d): Oops. Address error. dev_addr=0x%lx\n",
			__func__, __LINE__, (unsigned long)dev_addr );
	}
	__sdp_dmac( cpu_phys_addr, dev_phys_addr, size, EBUS_DMAC_READ );

	return;
}
EXPORT_SYMBOL(sdp_multi_read);

static int sdp_config_status(void)
{
	unsigned long flags = 0;
	int ret				= EBUS_SUCCESS;

	spin_lock_irqsave(&sdp_conf_lock, flags);
	/* Config/IO Transfer Statu:618h */
	while( ebus_rc_reg->config_io_ctl.confio_trans_status &	EBUS_CONFIGIO_IO_TRSBUSY_MASK ){
#if EBUS_TIME_CONFIG != 0
			udelay( EBUS_TIME_CONFIG );
#endif
			barrier();
		}

	/* Config/IO Transfer Status:618h */
	ret = ebus_rc_reg->config_io_ctl.confio_trans_status & EBUS_CONFIGIO_IO_COMPSTS_MASK;
	if(ret){
		printk("%s(%d): Oops. PCI macro error. ret=%d\n",__func__, __LINE__, ret );
	}
	spin_unlock_irqrestore(&sdp_conf_lock, flags);

	sdp_config_sync->syncCondition = 1;
	wake_up_interruptible(&sdp_config_sync->syncQueue);
	
	return ret;

}
static int sdp_mem_status(void)
{
	unsigned long flags = 0;
	int ret				= EBUS_SUCCESS;

	spin_lock_irqsave(&sdp_mem_lock, flags);

	/* Memory TLP Transfer Status Register:6A0h */
	while ( ebus_rc_reg->config_io_ctl.mem_tlp_trans_status &	EBUS_MTLPSTS_RTRSBUSY_MASK ){
#if EBUS_TIME_MEM != 0
		udelay( EBUS_TIME_MEM );
#endif
		barrier();
	}

	/* Memory TLP Transfer Status Register:6A0h */
	ret = ebus_rc_reg->config_io_ctl.mem_tlp_trans_status &	EBUS_MTLPSTS_COMPSTS_MASK;
	if(ret) {
		printk("%s(%d): Oops. PCI macro error. ret=%d\n",__func__, __LINE__, ret );
	}
	spin_lock_irqsave(&sdp_mem_lock, flags);

	sdp_mem_sync->syncCondition = 1;
	wake_up_interruptible(&sdp_mem_sync->syncQueue);


	return ret;
}
static int sdp_dma_status(void)
{
	int ret				= EBUS_SUCCESS;
	
	sdp_dma_sync->syncCondition = 1;
	wake_up_interruptible(&sdp_dma_sync->syncQueue);
	return ret;

}


int sdp_check_transfer_staus(int mode)
{

	if(mode==PCIE_ST_CFGIO_INT)
	{
		sdp_config_status();
	}
	else if(mode==PCIE_ST_MEM_INT)
	{
		sdp_mem_status();
	}
	else if(mode==PCIE_ST_DMA_INT)
	{
		sdp_dma_status();
	}
	
	return 0;
}

irqreturn_t sdp_pcie_irq(int irq, void *devid)
{
	u32 read_data =0;
	u32 val =0;
	
	read_data = readRegister(PCIE_GPR_BASE + 0x000000AC);	//check interrupt num
	read_data=((read_data>>15)&0x7);
	if(read_data!=0)
	{	
		switch(read_data)
		{
			case 0x1:
				val = PCIE_ST_CFGIO_INT;
				break;
			case 0x2:
				val = PCIE_ST_MEM_INT;
				break;
			case 0x3:
				val = PCIE_ST_DMA_INT;
				break;
			default:
				pr_err("[%s:%d] ERROR Pcie wrong interrupt!!\n",__func__,__LINE__);
				return IRQ_HANDLED;
				break;
		}
	}//else// pr_info("[%s:%d] DEBUG Pcie invaild interrupt!!\n");
				
	sdp_check_transfer_staus(val);		
	
	return IRQ_HANDLED;
}
static unsigned long 
sdp_open_config_window(struct pci_bus *bus, unsigned int devfn, int offset)
{
	unsigned int busnr;
	u32 slot, func;

	slot = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	busnr = bus->number;
                                                                           
	if(busnr == 0 && devfn == 0){
		ebus_rc_reg->config_io_ctl.rsv_destid = ((busnr << 8)| (slot << 3)| (func << 0));
		
	} else {
		//address = AHB_ADDR_PCI_CFG1(busnr, slot, func, offset);
		pr_err("[%s]Not support EndPoint!\n",__func__);
	}

	return 0;
}

static void sdp_close_config_window(void)
{
	/*
	 * Reassign base1 for use by prefetchable PCI memory
	 */

	/*
	 * And shrink base0 back to a 256M window (NOTE: MAP0 already correct)
	 */
	mb();
}

int sdp_read_config (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	u32 regVal;
	u8 val_b;
	u16 val_w;
	
	mutex_lock(&sdp_confg_mutex);
	
	sdp_open_config_window(bus, devfn, where);//wher°¡ offsetµÊ.

	switch (size) {
		case 1:
			sdp_read_config_byte(where, &val_b);
			regVal = val_b;
			break;

		case 2:
			sdp_read_config_word(where,&val_w);
			regVal = val_w;
			break;

		default:
			sdp_read_config_dword(where,&regVal);
			break;
	}
	sdp_close_config_window();

	mutex_unlock(&sdp_confg_mutex);
	
	*val = regVal;

	return PCIBIOS_SUCCESSFUL;

}
int sdp_write_config (struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{

	sdp_open_config_window(bus, devfn, where);

	mutex_lock(&sdp_confg_mutex);

	switch (size) {
		case 1:
			sdp_write_config_byte(where,(u8)val);
			break;

		case 2:
			sdp_write_config_dword(where,(u16)val);
			break;

		default:
			sdp_write_config_dword(where,(u32)val);
			break;
	}

	sdp_close_config_window();

	mutex_unlock(&sdp_confg_mutex);

	return PCIBIOS_SUCCESSFUL;
}


int sdp_pcie_setup_resources(struct resource **resource)
{
	if (request_resource(&iomem_resource, &pcie_io)) {
			   DBG(KERN_ERR "PCI io/config: unable to allocate non-prefetchable "
					  "memory region\n");
			   return -EBUSY;
	   }
	if (request_resource(&iomem_resource, &pcie_mem)) {
		   DBG(KERN_ERR "PCI mem: unable to allocate non-prefetchable "
				  "memory region\n");
		   return -EBUSY;
   }

	   /*
         * bus->resource[0] is the IO resource for this bus
         * bus->resource[1] is the mem resource for this bus
         * bus->resource[2] is the prefetch mem resource for this bus
         */
        resource[0] = &pcie_io;
        resource[1] = &pcie_mem;
        resource[2] = NULL;

 		pcie_va =(void*)ioremap_nocache(SDP_MEM_PHYS,SDP_SIZE);
        return 1;


}	
int __init sdp_pcie_setup(int nr, struct pci_sys_data *sys)
{
	int ret = 0;

	if (nr == 0) {
		sys->mem_offset = SDP_CONFIG_PHYS;
		if (request_resource(&iomem_resource, &pcie_io)) {
			   DBG(KERN_ERR "PCI io/config: unable to allocate non-prefetchable "
					  "memory region\n");
			   return -EBUSY;
	   }
		if (request_resource(&iomem_resource, &pcie_mem)) {
			   DBG(KERN_ERR "PCI mem: unable to allocate non-prefetchable "
					  "memory region\n");
			   return -EBUSY;
	   }

	   /*
         * bus->resource[0] is the IO resource for this bus
         * bus->resource[1] is the mem resource for this bus
         * bus->resource[2] is the prefetch mem resource for this bus
         */
 		pcie_va =(void*)ioremap_nocache(SDP_MEM_PHYS,SDP_SIZE);
        return 1;
	}

	return ret;
}

void __init sdp_pcie_preinit(void)
{
	u32 retVal	= 0,read_data	= 0;
	u64 timeout;

	ebus_rc_reg = NULL;
	ebus_rc_reg = (ebus_rc_reg_t*)ioremap(PA_PCIE_BASE,sizeof(ebus_rc_reg_t));

	if ( ebus_rc_reg == NULL ) {
		printk( KERN_ERR 
			"driver '%s' mapping IO resource is failed.\n",
			DRIVER_NAME );

		return;
	}

	spin_lock_init(&sdp_mem_lock);
	spin_lock_init(&sdp_conf_lock);

	/* SYSOC:PCIe PHY Reset Control reg. */
	read_data = readRegister	(SW_RESET1);	
	read_data|=(1<<27)|(1<<28); //27 link ,28 phy
	writeRegister	(SW_RESET1,read_data);

	/* Power CSR:408h */
	ebus_rc_reg->app_regi_block.power_csr &= EBUS_POWERCSR_MASK;
	//-----------------------
	// Process: Link Up
	//-----------------------
	
	read_data = readRegister	(PCIE_GPR_BASE + 0x000000AC);	// Waiting Link Up

	timeout = jiffies + HZ;
	while (!(read_data & 0x400)) { 
		cond_resched();// [10]: PCIE_ST_DL_UP
		read_data = readRegister	(PCIE_GPR_BASE + 0x000000AC);	// Waiting Link Up
		if(jiffies > timeout)
		{		
			pr_err("%s: timeout error!!!!!\n",__func__);
			return;
			break;
		}
	}

	printk(KERN_INFO "SDP PCI Express RC driver version 0.5\n");
			// did 0xA639 	, vid 	0x144D
	printk(KERN_INFO "SDP PCI Express RC VID 0x%lx PID 0x%lx\n",(ebus_rc_reg->conf_head.dev_vend & 0x0F),(ebus_rc_reg->conf_head.dev_vend >> 16)&0x0F);

	/* Power Management Control/Status:044h */
	ebus_rc_reg->pm_cap.power_manage_ctl |= EBUS_PMSTCTL;

	/* Root Control Register:09ch */
	ebus_rc_reg->pci_cap.root_cap_ctl |= EBUS_ROOTCTL;

	/* DMAC Mode Control:C04h (Set Demand Mode CH=0,1) */
	ebus_rc_reg->dma_ctl.dmac_mode_control = EBUS_DMAMODECTL; //set DMA ch 0

	/* Memory TLP Transfer Interrupt Mask Register:69Ch*/
	ebus_rc_reg->config_io_ctl.mem_tlp_interrupt_mask = EBUS_MTLPINRERR;

	/* DMAC Interrupt Mask:C0Ch*/
	ebus_rc_reg->dma_ctl.dmac_interrupt_mask = EBUS_DMAINTERR_MASK;

	/* Command Register:004h */
	ebus_rc_reg->conf_head.st_com |= EBUS_COMMAND;

	/* Set Max Payload Size:0x88h */ // Max Payload Size      (088h Read ) [14:12]: max_read_req_size [ 7: 5]: max_pl_size
	ebus_rc_reg->pci_cap.dev_st_ctl |=((MAX_PL_SIZE & 0x7)<<5|(MAX_READ_REQ_SIZE & 0x7)<<12);

//	ebus_rc_reg->config_io_ctl.dst_mem_low=(unsigned long)kmalloc((1<<12),GFP_KERNEL);

	init_waitqueue_head(&sdp_config_sync->syncQueue);
	init_waitqueue_head(&sdp_mem_sync->syncQueue);
	init_waitqueue_head(&sdp_dma_sync->syncQueue);

	/* Initialize End Point Configration register */
	sdp_config_init();

	
	retVal = request_irq(IRQ_PCI, sdp_pcie_irq, IRQF_SHARED, "PCIe irq", NULL);

	if(retVal < 0) printk("PCI RC request interrupt failed %d\n", retVal);

	return;


}
void __init sdp_pcie_postinit(void)
{

}

/**
 * ebus_exit - exit ebus driver.
 *
 * no return value
 */
static void __exit sdp_pcie_exit(void)
{

	iounmap( ebus_rc_reg );

}

MODULE_AUTHOR("....");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");

