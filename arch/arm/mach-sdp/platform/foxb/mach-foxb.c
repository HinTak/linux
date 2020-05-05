/*
 * mach-foxb.c
 *
 * Copyright (C) 2011 Samsung Electronics.co
 *
 * Authors:
 * SeungJun Heo <seungjun.heo@samsung.com>
 * Seihee Chon <sh.chon@samsung.com>
 * Sola Lee <sssol.lee@samsung.com>
 * Ikjoon Jang <ij.jang@samsung.com>: linux-3.8-x preparation, use common_clk api
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
#include <asm/smp_twd.h>
#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>

#include <mach/hardware.h>

extern void sdp_init_irq(void) __init; 
extern void sdp1207_init(void) __init;
extern void sdp1207_init_early(void) __init;
extern void sdp1207_iomap_init(void) __init;
extern void foxb_board_init(void) __init;
extern void sdp12xx_timer_init(void) __init;
extern int sdp1207_init_clocks(void) __init;
extern struct smp_operations sdp_smp_ops __initdata;

phys_addr_t sdp_sys_mem0_size = (128<< 20);
phys_addr_t sdp_sys_mem1_size = (128<< 20);
phys_addr_t sdp_mach_mem0_size = (512 << 20);
phys_addr_t sdp_mach_mem1_size = (512 << 20);
unsigned int sdp_revision_id = -1;	/* FIXME */

EXPORT_SYMBOL(sdp_sys_mem0_size);

struct sdp_mem_param_t {
	char magic[16];
	u32  sys_mem0_size;
	u32  mach_mem0_size;
	u32  sys_mem1_size;
	u32  mach_mem1_size;
	u32  sys_mem2_size;
	u32  mach_mem2_size;
};

static char mem_info_magic[] __initdata = "SamsungCCEP_memi";

struct sdp_mem_param_t *foxb_get_mem_param(void)
{
	struct sdp_mem_param_t *mparam = (struct sdp_mem_param_t *)(PAGE_OFFSET + 0xa00);
	return (memcmp(mparam->magic, mem_info_magic, 15)) ? NULL : mparam;
}

static int __initdata sdp_mem_param_valid;
static void __init foxb_setup_meminfo(void)
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
	minfo->nr_banks = nbank;

	pr_info("Board Memory : %dMB(%dMB+%dMB), Kernel Memory : %dMB(%dMB+%dMB)\n",
		(sdp_mach_mem0_size + sdp_mach_mem1_size) >> 20,
		sdp_mach_mem0_size >> 20, sdp_mach_mem1_size >> 20,
		(sdp_sys_mem0_size + sdp_sys_mem1_size ) >> 20,
		sdp_sys_mem0_size >> 20, sdp_sys_mem1_size >> 20);
}

/* 27 Jan 2014: for "FoxB 2013 onboot + 2014 kernel" support, memory map hotfix */
#define HOTFIX_384MB_SYSMEM0_SIZE		(207)
#define HOTFIX_384MB_SYSMEM1_SIZE		(126)
static int __init foxb_model_hotfix(char *p)
{
	struct sdp_mem_param_t *mparam = foxb_get_mem_param();
	if (!memcmp(p, "384MB", 5) && mparam) {
		sdp_sys_mem0_size = HOTFIX_384MB_SYSMEM0_SIZE << 20;
		sdp_sys_mem1_size = HOTFIX_384MB_SYSMEM1_SIZE << 20;
		pr_info("Fox-B meminfo hotfix!");
		foxb_setup_meminfo();
	}
	return 0;
}
early_param("model", foxb_model_hotfix);

static void __init foxb_setup_mem_params(void)
{
	struct sdp_mem_param_t *gp_mem_param = foxb_get_mem_param();
	
	if(gp_mem_param) {
		sdp_sys_mem0_size = gp_mem_param->sys_mem0_size << 20;
		sdp_mach_mem0_size = gp_mem_param->mach_mem0_size << 20;
		sdp_sys_mem1_size = gp_mem_param->sys_mem1_size << 20;
		sdp_mach_mem1_size = gp_mem_param->mach_mem1_size << 20;
		if(gp_mem_param->magic[15] == 'i')
			sdp_revision_id = 0;
		else
			sdp_revision_id = (u32) (gp_mem_param->magic[15] - '0');
		sdp_mem_param_valid = 1;
		pr_info ("sdp_mem_params is valid, use this!\n");
	}
	foxb_setup_meminfo();
}

#ifdef CONFIG_HDMA_DEVICE
struct hdmaregion  {
	phys_addr_t start;
	phys_addr_t size;
	bool check_fatal_signals;
	struct {
		phys_addr_t size;
		phys_addr_t start;
	} aligned;
	/* page is returned/used by dma-contiguous API to allocate/release
	   memory from contiguous pool */
	struct page *page;
	unsigned int count;
	struct device *dev;
	struct device dev2;
};

#define MAX_CMA_REGIONS 16

struct cmainfo  {
	int nr_regions;
	struct hdmaregion region[MAX_CMA_REGIONS];
};

extern struct cmainfo hdmainfo;
extern struct meminfo meminfo;
#define MAX_OF_BANK 5
unsigned long hdma_size_of_bank[MAX_OF_BANK];
#endif

int sdp_get_mem_cfg(int nType)
{
	switch(nType)
	{
	case 0:
#ifdef CONFIG_HDMA_DEVICE
		return sdp_sys_mem0_size - hdma_size_of_bank[0];
#else
		return sdp_sys_mem0_size;
#endif
	case 1:
		return sdp_mach_mem0_size;
	case 2:
#ifdef CONFIG_HDMA_DEVICE
		return sdp_sys_mem1_size - hdma_size_of_bank[1];
#else
		return sdp_sys_mem1_size;
#endif
	case 3:
		return sdp_mach_mem1_size;
	}
	return -1;
}
EXPORT_SYMBOL(sdp_get_mem_cfg);

static void __init sdp_gic_init_irq(void)
{ 
#ifdef CONFIG_ARM_GIC
	gic_init(0, IRQ_LOCALTIMER, (void __iomem *) VA_GIC_DIST_BASE, (void __iomem *) VA_GIC_CPU_BASE);
#endif	
}

static void __init foxb_map_io(void)
{
	//initialize iomap of special function register address
	sdp1207_iomap_init();
}

static void __init foxb_init_early(void)
{
	/* early_param("mem") had set the meminfo */
	sdp1207_init_early();
}

static int __init foxb_early_revision_id(char *p)
{
	if (sdp_revision_id == -1) {
		pr_warn ("sdp_revision_id already set (%d), ignore this: %s.\n",
				sdp_revision_id, p);
	} else {
		sdp_revision_id = simple_strtoul(p, NULL, 10);
		pr_info ("foxb sdp1207 revision id = %u\n", sdp_revision_id);
	}
	return 0;
}
early_param("sdp_revision_id", foxb_early_revision_id);

/*
 * Pick out the memory size.  We look for sdp_board_mem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init foxb_early_board_mem(char *p)
{
	phys_addr_t size, start, ksize;
	char *endp;
	static int bank __initdata = 0;
	
	if (sdp_mem_param_valid) {
		pr_warn ("memory information is already initialized by sdp params, ignore %s.\n", p);
		return 0;
	}
	if (bank >= 2) {
		pr_err ("too much sdp_board_mem params!\n");
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

	if (bank == 0) {
		if (start && start != MACH_MEM0_BASE) {
			pr_warn ("Bank%d base address should be 0x%x, but you gave 0x%x\n",
					bank, MACH_MEM0_BASE, start);
			start = MACH_MEM0_BASE;
		}
		sdp_sys_mem0_size = ksize;
		sdp_mach_mem0_size = size;
		sdp_sys_mem1_size = 0;
		sdp_mach_mem1_size = 0;
	} else {
		if (start && start != MACH_MEM1_BASE) {
			pr_warn ("Bank%d base address should be 0x%x, but you gave 0x%x\n",
					bank, MACH_MEM1_BASE, start);
			start = MACH_MEM1_BASE;
		}
		sdp_sys_mem1_size = ksize;
		sdp_mach_mem1_size = size;
	}
	pr_info ("sdp_board_mem for bank%d  is set, override meminfo.\n", bank);
	
	bank++;
	
	foxb_setup_meminfo();

	return 0;
}
early_param("sdp_board_mem", foxb_early_board_mem);


static void __init foxb_init(void)
{
	sdp1207_init();
	//foxb_board_init();
}

static void __init foxb_fixup(struct tag *tags, char ** cmdline, struct meminfo *minfo)
{
	foxb_setup_mem_params();
}

DEFINE_TWD_LOCAL_TIMER(twd_local_timer, PA_TWD_BASE, IRQ_LOCALTIMER);

static void __init foxb_timer_init(void)
{
	/* XXX: init_early is right place to call 'init_clocks',
	 * but we're using kmalloc... */
	sdp1207_init_clocks();

	sdp12xx_timer_init();
	twd_local_timer_register(&twd_local_timer);
}

static struct sys_timer foxb_timer = {
	.init = foxb_timer_init,
};

#ifdef CONFIG_HDMA_DEVICE
extern void hdma_regions_reserve(void);

static void __init foxb_reserve(void)
{
	int i,j;

	hdma_regions_reserve();

	for(i=0; i<meminfo.nr_banks; i++) {
	for(j=0; j<hdmainfo.nr_regions; j++) {
		if((meminfo.bank[i].start <= hdmainfo.region[j].start) &&
			((meminfo.bank[i].start + meminfo.bank[i].size) >=
			(hdmainfo.region[j].start + hdmainfo.region[j].size)))
			hdma_size_of_bank[i] += hdmainfo.region[j].size;
	}
	}
}
#endif

MACHINE_START(SDP1207_FOXB, "Samsung SDP1207 evaluation board")
	.atag_offset	= 0x100,
	.map_io		= foxb_map_io,
	.fixup		= foxb_fixup,
#ifdef CONFIG_ARM_GIC
	.init_irq	= sdp_gic_init_irq,
	.handle_irq	= gic_handle_irq,
#else
	.init_irq	= sdp_init_irq,
#endif
	.timer		= &foxb_timer,
	.init_early	= foxb_init_early,
	.init_machine	= foxb_init,
	.smp		= smp_ops(sdp_smp_ops),
#ifdef CONFIG_HDMA_DEVICE
	.reserve        = foxb_reserve,
#endif
MACHINE_END

