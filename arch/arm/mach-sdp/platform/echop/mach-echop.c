/*
 * mach-echop.c
 *
 * Copyright (C) 2011-2013 Samsung Electronics.co
 * SeungJun Heo <seungjun.heo@samsung.com>
 * Ikjoon Jang <ij.jang@samsung.com>
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/export.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/smp_scu.h>
#include <asm/setup.h>
#include <asm/smp_twd.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>

#include <mach/hardware.h>

phys_addr_t sdp_sys_mem0_size = SYS_MEM0_SIZE;
phys_addr_t sdp_sys_mem1_size = SYS_MEM1_SIZE;
phys_addr_t sdp_sys_mem2_size = SYS_MEM2_SIZE;
phys_addr_t sdp_mach_mem0_size = MACH_MEM0_SIZE;
phys_addr_t sdp_mach_mem1_size = MACH_MEM1_SIZE;
phys_addr_t sdp_mach_mem2_size = MACH_MEM2_SIZE;

EXPORT_SYMBOL(sdp_sys_mem0_size);
EXPORT_SYMBOL(sdp_sys_mem1_size);

extern void sdp_init_irq(void) __init; 
extern void sdp1106_init(void) __init;
extern void sdp1106_init_early(void) __init;
extern void sdp1106_iomap_init(void) __init;
extern int sdp1106_init_clocks(void) __init;

extern void echop_board_init(void) __init;

extern struct smp_operations sdp_smp_ops __initdata;
extern void sdp12xx_timer_init(void) __init;

unsigned int sdp_revision_id;
EXPORT_SYMBOL(sdp_revision_id);

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

extern struct meminfo meminfo;

static int __initdata sdp_mem_param_valid;
static char mem_info_magic[] __initdata = MEM_INFO_MAGIC;

static void __init echop_setup_meminfo(void)
{
	struct meminfo *minfo = &meminfo;
	int nbank = 0;

	if (sdp_sys_mem0_size != 0) {
		minfo->bank[nbank].start = MACH_MEM0_BASE;
		minfo->bank[nbank].size =  sdp_sys_mem0_size;
		minfo->bank[nbank].highmem = 0;
		nbank++;
	}
	if(sdp_sys_mem1_size != 0) {
		minfo->bank[nbank].start = MACH_MEM1_BASE;
		minfo->bank[nbank].size =  sdp_sys_mem1_size;
		minfo->bank[nbank].highmem = 0;
		nbank++;
	}
	if(sdp_sys_mem2_size != 0) {
		minfo->bank[nbank].start = MACH_MEM2_BASE;
		minfo->bank[nbank].size =  sdp_sys_mem2_size;
		minfo->bank[nbank].highmem = 0;
		nbank++;
	}
	minfo->nr_banks = nbank;

	pr_info("Board Memory : %dMB(%dMB+%dMB+%dMB), Kernel Memory : %dMB(%dMB+%dMB+%dMB)\n",
		(sdp_mach_mem0_size + sdp_mach_mem1_size + sdp_mach_mem2_size) >> 20,
		sdp_mach_mem0_size >> 20, sdp_mach_mem1_size >> 20 , sdp_mach_mem2_size >> 20,
		(sdp_sys_mem0_size + sdp_sys_mem1_size + sdp_sys_mem2_size) >> 20,
		sdp_sys_mem0_size >> 20, sdp_sys_mem1_size >> 20, sdp_sys_mem2_size >> 20);
}

static void __init echop_setup_mem_params(void)
{
	struct sdp_mem_param_t *gp_mem_param =
		(struct sdp_mem_param_t *)(PAGE_OFFSET + 0xa00);
	
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

		sdp_mem_param_valid = 1;
		pr_info ("sdp_mem_params is valid, use this!\n");
	}
	echop_setup_meminfo();
}

int sdp_get_mem_cfg(int nType)
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
	}
	return -1;
}

EXPORT_SYMBOL(sdp_get_mem_cfg);

void __init sdp_gic_init_irq(void)
{ 
#ifdef CONFIG_ARM_GIC
	gic_init(0, IRQ_LOCALTIMER, (void __iomem *) VA_GIC_DIST_BASE, (void __iomem *) VA_GIC_CPU_BASE);
#endif	
}

static void __init echop_map_io(void)
{
	//initialize iomap of special function register address
	sdp1106_iomap_init();
}

static void __init echop_init_early(void)
{
	sdp1106_init_early();
}

static void __init echop_init(void)
{
	sdp1106_init();
	//echop_board_init();
}

/* echop early params */
static int __init echop_early_revision_id(char *p)
{
	sdp_revision_id = simple_strtoul(p, NULL, 10);
	printk (KERN_INFO "echop sdp1106 revision id = %u\n", sdp_revision_id);
	return 0;
}
early_param("sdp_revision_id", echop_early_revision_id);

/*
 * Pick out the memory size.  We look for sdp_board_mem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init echop_early_board_mem(char *p)
{
	phys_addr_t size, start, ksize;
	char *endp;
	static int bank __initdata = 0;
	
	if (sdp_mem_param_valid) {
		pr_warn ("memory information is already initialized by sdp params, ignore %s.\n", p);
		return 0;
	}
	if (bank >= 3) {
		pr_err ("You gave non-existed ddr bank info for sdp mem params, ignore %s\n", p);
		return -EINVAL;	
	}

	size  = memparse(p, &endp);
	if (*endp == ',') {
		ksize = size;
		size  = memparse(endp + 1, &endp);
	} else
		ksize = sdp_sys_mem0_size;

	if (*endp == '@')
		start = memparse(endp + 1, &endp);
	else
		start = 0;

	switch (start) {
	case MACH_MEM0_BASE:
		sdp_sys_mem0_size = ksize;
		sdp_mach_mem0_size = size;
		break;
	case MACH_MEM1_BASE:
		sdp_sys_mem1_size = ksize;
		sdp_mach_mem1_size = size;
		break;
	case MACH_MEM2_BASE:
		sdp_sys_mem2_size = ksize;
		sdp_mach_mem2_size = size;
		break;
	default:
		pr_err ("Bad memory configuration for sdp_board_mem params\n");
		return -EINVAL;
	}
	pr_info ("sdp_board_mem for bank%d  is set, override meminfo.\n", bank);
	
	if (bank == 0) {
		sdp_sys_mem1_size = 0;
		sdp_sys_mem2_size = 0;
	}
	bank++;
	
	echop_setup_meminfo();

	return 0;
}
early_param("sdp_board_mem", echop_early_board_mem);

static void __init echop_fixup(struct tag *tags, char ** cmdline, struct meminfo *minfo)
{
	echop_setup_mem_params();
}

#ifdef CONFIG_HAVE_ARM_TWD
DEFINE_TWD_LOCAL_TIMER(twd_local_timer, PA_TWD_BASE, IRQ_LOCALTIMER);
#endif

static void __init echop_timer_init(void)
{
	/* XXX: init_early is right place to call 'init_clocks',
	 * but we're using kmalloc... */
	sdp1106_init_clocks();

	sdp12xx_timer_init();
#ifdef CONFIG_HAVE_ARM_TWD
	twd_local_timer_register(&twd_local_timer);
#endif
}

static struct sys_timer echop_timer = {
	.init = echop_timer_init,
};

MACHINE_START(SDP1106_ECHOP, "Samsung SDP1106 evaluation board")
	.atag_offset	= 0x100,
	.map_io		= echop_map_io,
	.fixup		= echop_fixup,
#ifdef CONFIG_ARM_GIC
	.init_irq	= sdp_gic_init_irq,
	.handle_irq	= gic_handle_irq,
#else
	.init_irq	= sdp_init_irq,
#endif
	.timer		= &echop_timer,
	.init_early	= echop_init_early,
	.init_machine	= echop_init,
	.smp		= smp_ops(sdp_smp_ops),
MACHINE_END

