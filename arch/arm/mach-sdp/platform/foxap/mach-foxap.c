/*
 * mach-foxap.c
 *
 * Copyright (C) 2012 Samsung Electronics.co
 * SeungJun Heo <seungjun.heo@samsung.com>
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/smp_scu.h>
#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>

#include <mach/hardware.h>
#include <mach/sdp_smp.h>

#define _BETWEEN(start, size, val) ( ((start) <= (val)) && (((start)+(size)-1) >= (val)) )

unsigned int sdp_sys_mem0_size = SYS_MEM0_SIZE;
unsigned int sdp_sys_mem1_size = 0;
unsigned int sdp_sys_mem2_size = 0;
unsigned int sdp_mach_mem0_size = MACH_MEM0_SIZE;
unsigned int sdp_mach_mem1_size = MACH_MEM1_SIZE;
unsigned int sdp_mach_mem2_size = MACH_MEM2_SIZE;
EXPORT_SYMBOL(sdp_sys_mem0_size);
EXPORT_SYMBOL(sdp_sys_mem1_size);
EXPORT_SYMBOL(sdp_sys_mem2_size);

extern void sdp_init_irq(void); 
extern void sdp1202_init(void);
extern void sdp1202_iomap_init(void);
extern void sdp12xx_timer_init(void) __init;
extern int sdp1202_init_clocks(void) __init;
extern struct smp_operations sdp_smp_ops __initdata;
#ifdef CONFIG_ARM_ARCH_TIMER
extern int arch_timer_register_irq(int irq);
#endif

unsigned int sdp_revision_id = 0;
EXPORT_SYMBOL(sdp_revision_id);

bool sdp_is_dual_mp;

#define MEM_INFO_MAGIC	"SamsungCCEP_memi"

struct sdp_mem_param_t {
	char magic[16];
	u32  sys_mem0_size;
	u32  mach_mem0_size;
	u32  sys_mem1_size; 
	u32  mach_mem1_size;
	u32  sys_mem2_size; 
	u32  mach_mem2_size;
};

static struct sdp_mem_param_t * gp_mem_param __initdata =
		(struct sdp_mem_param_t*) (0xC0000000 + 0xA00);
static char mem_info_magic[] __initdata = MEM_INFO_MAGIC;

static void __init sdp_gic_init_irq(void)
{ 
#ifdef CONFIG_ARM_GIC
	gic_init_bases(0, IRQ_LOCALTIMER, (void __iomem *) VA_GIC_DIST_BASE, (void __iomem *) VA_GIC_CPU_BASE, 0x8000, NULL);
#endif	
}

static void __init foxap_map_io(void)
{
	//initialize iomap of special function register address
	sdp1202_iomap_init();
}

static void __init foxap_init_early(void)
{
	/* early_param("mem") had set the meminfo */
	extern struct meminfo meminfo;
	int i;
	for(i = 0; i < meminfo.nr_banks; i++) {
		pr_debug("foxap sdp1202 memory bank%d %4uM@%x\n",
			i, bank_phys_size(&meminfo.bank[i])/SZ_1M, bank_phys_start(&meminfo.bank[i]));
	}

	for(i = 0; i < meminfo.nr_banks; i++) {
		if(_BETWEEN(MACH_MEM0_BASE, MACH_MEM0_SIZE, bank_phys_start(&meminfo.bank[0])))
		{
			sdp_sys_mem0_size += bank_phys_size(&meminfo.bank[i]);
		}
		else
		{
			printk(KERN_ERR "foxap memory setting fault!!!\n");
		}
	}

	printk (KERN_INFO "foxap sdp1202 memory kernel %4uM\n",	sdp_sys_mem0_size/SZ_1M);

//	sdp1202_init_early();
}

#if !defined(CONFIG_ARCH_SDP1202_EVAL)
#define ASM_LDR_PC(offset)      (0xe59ff000 + (offset - 0x8))
static int foxap_install_warp_percpu(int cpu)
{
	void __iomem *base;
	u32 instr_addr = 0x48 - cpu * 4;

	base = ioremap(0x0, 512);

	writel_relaxed(ASM_LDR_PC(instr_addr), base);
	writel_relaxed(virt_to_phys(sdp_secondary_startup), base + instr_addr);
	dmb();

	iounmap(base);

	mdelay(10);

	return 0;
}

struct sdp_power_ops sdp1202_power_ops = {
	.install_warp	= foxap_install_warp_percpu,
};
#endif

static void __init foxap_init(void)
{
	sdp1202_init();
}

static void __init 
foxap_fixup(struct tag *tags, char ** cmdline, struct meminfo *minfo)
{
	int nbank = 0;
	
	if(memcmp(gp_mem_param->magic, mem_info_magic, 15) == 0){
		sdp_sys_mem0_size = gp_mem_param->sys_mem0_size << 20;
		sdp_mach_mem0_size = gp_mem_param->mach_mem0_size << 20;
		sdp_sys_mem1_size = gp_mem_param->sys_mem1_size << 20;
		sdp_mach_mem1_size = gp_mem_param->mach_mem1_size << 20;
		sdp_sys_mem2_size = gp_mem_param->sys_mem2_size << 20;
		sdp_mach_mem2_size = gp_mem_param->mach_mem2_size << 20;
		if(gp_mem_param->magic[15] == 'i')
			sdp_revision_id = 0;
		else
			sdp_revision_id = (u32) (gp_mem_param->magic[15] - '0');
	}
	else{
		printk(KERN_INFO"[%s] Set Default memory configruation\n",__FUNCTION__);
	}

	/* set bank0 */
	minfo->bank[nbank].start = PHYS_OFFSET;
	minfo->bank[nbank].size = sdp_sys_mem0_size;
	nbank++;
	minfo->nr_banks = nbank;

	printk("Board Memory : %d(%d+%d+%d)MB, Kernel Memory : %dMB\n"
			, (sdp_mach_mem0_size + sdp_mach_mem1_size + sdp_mach_mem2_size) >> 20
			, sdp_mach_mem0_size >> 20, sdp_mach_mem1_size >> 20, sdp_mach_mem2_size >> 20
			, sdp_sys_mem0_size >> 20);

#if !defined(CONFIG_ARCH_SDP1202_EVAL)
	sdp_set_power_ops(&sdp1202_power_ops);
#endif
}

extern int sdp1202_get_revision_id(void);

static void __init foxap_timer_init(void)
{
	/* XXX: init_early is right place to call 'init_clocks',
	 * but we're using kmalloc... */
	int irq;
	sdp1202_init_clocks();
	sdp12xx_timer_init();
#ifdef CONFIG_ARM_ARCH_TIMER
	if(sdp1202_get_revision_id())
	{
#ifdef CONFIG_ARM_TRUSTZONE
		irq = 30;
#else
		irq = 29;
#endif
	}
	else
		irq = IRQ_LOCALTIMER;
	arch_timer_register_irq(irq);
#endif
}

static struct sys_timer foxap_timer = {
	.init = foxap_timer_init,
};

unsigned int sdp_get_mem_cfg(int nType)
{
	switch(nType)
	{
		case 0:
			return sdp_sys_mem0_size;
		case 1:
			return sdp_mach_mem0_size;
		case 2:
			return sdp_sys_mem1_size;
		case 3:
			return sdp_mach_mem1_size;
		case 4:
			return sdp_sys_mem2_size;
		case 5:
			return sdp_mach_mem2_size;
		default:
			return (u32) -1;
	}
	return (u32) -1;
}

EXPORT_SYMBOL(sdp_get_mem_cfg);

MACHINE_START(SDP1202_FOXAP, "Samsung SDP1202 evaluation board")
	.atag_offset  = 0x100,
	.map_io		= foxap_map_io,
	.fixup		= foxap_fixup,
	.init_irq	= sdp_gic_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer		= &foxap_timer,
//	.init_early	= foxap_init_early,
	.init_machine	= foxap_init,
	.smp		= smp_ops(sdp_smp_ops),
MACHINE_END

