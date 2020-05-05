/*
 * kdml_menu.c
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include "kdebugd/kdebugd.h"
#include "kdml/kdml_meminfo.h"

/* Flag is used to turn kernel meminfo on or off*/
static int kdml_ctr_kernel_status;

/* Flag is used to turn user meminfo on or off*/
static int kdml_ctr_user_status;

/* Flag is used to turn meminfo summary on or off*/
static int kdml_ctr_summary_status;

/* index to print meminfo header */
static int kdml_meminfo_index;


/* This function is to show header */
static void kernel_meminfo_header(void)
{
	PRINT_KD
		("time total       free         slab         vmalloc      ioremap      pagetable    "
		 "kernelstack  zram        buddy        sum_kernel\n");
	PRINT_KD
		("==== =========== ===========  ===========  ===========  ===========  ===========  "
		 "===========  =========== ===========  =========\n");
}

/* This function is to show header */
static void user_meminfo_header(void)
{
	PRINT_KD
		("time page_cache  act_anon    inact_anon   act_file    inactive_file  unevictable anon_pages  "
		 "mapped      shmem       sum_user\n");
	PRINT_KD
		("==== =========== =========== ===========  =========== ===========    =========== =========== "
		 "=========== =========== ==========\n");
}

/* This function is to show header */
static void meminfo_summary_header(void)
{
	PRINT_KD("Time Total            Kernel         User\n");
	PRINT_KD("==== ============    ============    ==========\n");
}

/* show kernel memory stats */
static int kdbg_show_kernel_meminfo(void)
{
	struct kernel_mem_usage kernel_mem_info;

	if (!kdml_ctr_kernel_status)
		return -1;

	if ((kdml_meminfo_index % 20) == 0)
		kernel_meminfo_header();

	get_kernel_mem_usage(&kernel_mem_info);
	PRINT_KD("%04ld %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %12lu K %12lu K\n",
			kdbg_get_uptime(),
			kernel_mem_info.total_mem_size,
			kernel_mem_info.free_mem_size,
			kernel_mem_info.slab_size,
			kernel_mem_info.vmallocused_size,
			kernel_mem_info.ioremap_size,
			kernel_mem_info.pagetable_size,
			kernel_mem_info.kernelstack_size,
			kernel_mem_info.zram_size,
			kernel_mem_info.buddy_size,
			kernel_mem_info.sum_kernel_size);

	kdml_meminfo_index++;
	return 0;
}

#ifdef CONFIG_VMALLOCUSED_PLUS
/* Flag is used to turn meminfo summary on or off*/
static int kdml_ctr_vmalloc_status;

/* This function is to show header */
static void vmallocinfo_header(void)
{
	PRINT_KD("time  VmallocUsed [vm_map_ram ioremap  vmalloc    vmap   usermap  vpages overlapped] VmallocChunk\n");
	PRINT_KD(" sec      kB          kB         kB       kB       kB      kB       kB       kB           kB\n");
	PRINT_KD("====  ===========  ========== ======== ======== ======== ======== ======== ========  ============\n");
}

/* show kernel memory stats */
static int kdbg_show_vmallocinfo(void)
{
	struct vmalloc_usedinfo vmallocused = {0};
	struct vmalloc_info vmi;

	if (!kdml_ctr_vmalloc_status)
		return -1;

	if ((kdml_meminfo_index % 20) == 0)
		vmallocinfo_header();

	get_vmallocused(&vmallocused);
	get_vmalloc_info(&vmi);
	PRINT_KD("%04ld  %8lu    %8lu  %8lu %8lu %8lu %8lu %8lu %8lu    %8lu\n",
			kdbg_get_uptime(),
			vmi.used >> 10,
			vmallocused.vmapramsize >> 10,
			vmallocused.ioremapsize >> 10,
			vmallocused.vmallocsize >> 10,
			vmallocused.vmapsize >> 10,
			vmallocused.usermapsize >> 10,
			vmallocused.vpagessize >> 10,
			vmallocused.overlappedsize >> 10,
			vmi.largest_chunk >> 10);

	kdml_meminfo_index++;
	return 0;
}

/* Turn the prints of memusage summary on
 * or off depending on the previous status.
 */
int kdml_ctr_meminfo_vmalloc_on_off(void)
{
	kdml_ctr_vmalloc_status = (kdml_ctr_vmalloc_status) ? 0 : 1;

	if (kdml_ctr_vmalloc_status)
		PRINT_KD("Meminfo Summary Dump ON\n");
	else
		PRINT_KD("Meminfo Summary Dump OFF\n");

	return 0;
}
#else
int kdml_ctr_meminfo_vmalloc_on_off(void)
{
	PRINT_KD("Enable in config: CONFIG_VMALLOCUSED_PLUS!!\n");
	return -1;
}
#endif /* CONFIG_VMALLOCUSED_PLUS */

/* show user memory stats */
static int kdbg_show_user_meminfo(void)
{
	struct user_mem_usage user_mem_info;

	if (!kdml_ctr_user_status)
		return -1;

	if ((kdml_meminfo_index % 20) == 0)
		user_meminfo_header();

	get_user_mem_usage(&user_mem_info);
	PRINT_KD("%04ld %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K %9lu K\n",
			kdbg_get_uptime(),
			user_mem_info.page_cache_size,
			user_mem_info.active_anon_size,
			user_mem_info.inactive_anon_size,
			user_mem_info.active_file_size,
			user_mem_info.inactive_file_size,
			user_mem_info.unevictable_size,
			user_mem_info.anon_pages_size,
			user_mem_info.mapped_size,
			user_mem_info.shmem_size,
			user_mem_info.sum_user_size);

	kdml_meminfo_index++;
	return 0;
}

/* show summary stats */
static int kdbg_show_meminfo_summary(void)
{
	struct kernel_mem_usage kernel_mem_info;
	struct user_mem_usage user_mem_info;
	unsigned long user, kernel;


	if (!kdml_ctr_summary_status)
		return -1;

	if ((kdml_meminfo_index % 20) == 0)
		meminfo_summary_header();

	get_kernel_mem_usage(&kernel_mem_info);
	get_user_mem_usage(&user_mem_info);
	kernel = kernel_mem_info.sum_kernel_size;
	user = user_mem_info.sum_user_size;

	PRINT_KD("%04ld %10lu K %10lu K %10lu K\n",
			kdbg_get_uptime(),
			user + kernel, kernel, user);

	kdml_meminfo_index++;
	return 0;
}

/* Register function for kernel and user memory usage */
int kdml_ctr_register_meminfo_functions(void)
{
	int ret = 0;

	ret = register_counter_monitor_func(kdbg_show_kernel_meminfo);
	if (ret < 0) {
		PRINT_KD("WARN: Fail to Register Kernel mem usage function\n");
		goto err_out;
	}

	ret = register_counter_monitor_func(kdbg_show_user_meminfo);
	if (ret < 0) {
		PRINT_KD("WARN: Fail to Register User mem usage function\n");
		goto err_unregister_kern_info;
	}

	ret = register_counter_monitor_func(kdbg_show_meminfo_summary);
	if (ret	< 0) {
		PRINT_KD("WARN: Fail to Register mem usage summary function\n");
		goto err_unregister_user_info;
	}

#ifdef CONFIG_VMALLOCUSED_PLUS
	ret = register_counter_monitor_func(kdbg_show_vmallocinfo);
	if (ret	< 0) {
		PRINT_KD("WARN: Fail to Register mem usage summary function\n");
		goto err_unregister_summary;
	}
#endif
	return 0;

#ifdef CONFIG_VMALLOCUSED_PLUS
err_unregister_summary:
	unregister_counter_monitor_func(kdbg_show_meminfo_summary);
#endif
err_unregister_user_info:
	unregister_counter_monitor_func(kdbg_show_user_meminfo);
err_unregister_kern_info:
	unregister_counter_monitor_func(kdbg_show_kernel_meminfo);
err_out:
	return ret;
}

/* UnRegister function for kernel and user memory usage */
void kdml_ctr_unregister_meminfo_functions(void)
{
	if (unregister_counter_monitor_func(kdbg_show_meminfo_summary) < 0) {
		PRINT_KD("WARN: Fail to UNRegister kdbg_show_meminfo_summary function\n");
	}

	if (unregister_counter_monitor_func(kdbg_show_user_meminfo) < 0) {
		PRINT_KD("WARN: Fail to UNRegister kdbg_show_user_meminfo function\n");
	}

	if (unregister_counter_monitor_func(kdbg_show_kernel_meminfo) < 0) {
		PRINT_KD("WARN: Fail to UNRegister kdbg_show_kernel_meminfo function\n");
	}

#ifdef CONFIG_VMALLOCUSED_PLUS
	if (unregister_counter_monitor_func(kdbg_show_vmallocinfo) < 0)
		PRINT_KD("WARN: Fail to UNRegister kdbg_show_vmallocinfo function\n");
#endif

}

void kdml_ctr_meminfo_turnoff_messages(void)
{
	if (kdml_ctr_kernel_status) {
		kdml_ctr_kernel_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Kernel Meminfo Dump OFF\n");
	}

	if (kdml_ctr_user_status) {
		kdml_ctr_user_status = 0;
		PRINT_KD("\n");
		PRINT_KD("User Meminfo Dump OFF\n");
	}

	if (kdml_ctr_summary_status) {
		kdml_ctr_summary_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Meminfo Summary Dump OFF\n");
	}

#ifdef CONFIG_VMALLOCUSED_PLUS
	if (kdml_ctr_vmalloc_status) {
		kdml_ctr_vmalloc_status = 0;
		PRINT_KD("\n");
		PRINT_KD("Vmalloc Dump OFF\n");
	}
#endif

	/* reset meminfo index to print header next time */
	kdml_meminfo_index = 0;
}

/* Turn the prints of kernel memusage on
 * or off depending on the previous status.
 */
void kdml_ctr_kernel_meminfo_on_off(void)
{
	kdml_ctr_kernel_status = (kdml_ctr_kernel_status) ? 0 : 1;

	if (kdml_ctr_kernel_status)
		PRINT_KD("Kernel Meminfo Dump ON\n");
	else
		PRINT_KD("Kernel Meminfo Dump OFF\n");
}

/* Turn the prints of user memusage on
 * or off depending on the previous status.
 */
void kdml_ctr_user_meminfo_on_off(void)
{
	kdml_ctr_user_status = (kdml_ctr_user_status) ? 0 : 1;

	if (kdml_ctr_user_status)
		PRINT_KD("User Meminfo Dump ON\n");
	else
		PRINT_KD("User Meminfo Dump OFF\n");
}

/* Turn the prints of memusage summary on
 * or off depending on the previous status.
 */
void kdml_ctr_meminfo_summary_on_off(void)
{
	kdml_ctr_summary_status = (kdml_ctr_summary_status) ? 0 : 1;

	if (kdml_ctr_summary_status)
		PRINT_KD("Meminfo Summary Dump ON\n");
	else
		PRINT_KD("Meminfo Summary Dump OFF\n");
}

