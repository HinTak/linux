/*
 * This file contains shadow memory manipulation code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 *
 * Some of code borrowed from https://github.com/xairy/linux by
 *        Andrey Konovalov <adech.fo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define DISABLE_BRANCH_PROFILING

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/kasan.h>

#include "kasan.h"
#include "../slab.h"

#ifndef CONFIG_KASAN_SHADOW_OFFSET
unsigned int __read_mostly kasan_initialized;

static unsigned long kasan_shadow_start;
static unsigned long kasan_shadow_end;
unsigned long kasan_vmalloc_shadow_start;
static unsigned long kasan_vmalloc_shadow_end;

unsigned long __read_mostly kasan_shadow_offset;
#endif

#ifdef CONFIG_KASAN_GLOBALS
#define START_VADDR MODULES_VADDR
#else
#define START_VADDR PAGE_OFFSET
#endif

static inline bool addr_is_in_mem(unsigned long addr)
{
	bool ret = false;

	ret = (addr >= START_VADDR && addr < (unsigned long)high_memory);

#ifdef CONFIG_KASAN_VMALLOC
	ret = ret || is_vmalloc_addr((void *)addr);
#endif

	return ret;
}

/*
 * Poisons the shadow memory for 'size' bytes starting from 'addr'.
 * Memory addresses should be aligned to KASAN_SHADOW_SCALE_SIZE.
 */
static void kasan_poison_shadow(const void *address, size_t size, u8 value)
{
	void *shadow_start, *shadow_end;

	shadow_start = kasan_mem_to_shadow(address);
	shadow_end = kasan_mem_to_shadow(address + size - 1) + 1;

	memset(shadow_start, value, shadow_end - shadow_start);
}

void kasan_unpoison_shadow(const void *address, size_t size)
{
	kasan_poison_shadow(address, size, 0);

	if (size & KASAN_SHADOW_MASK) {
		u8 *shadow = (u8 *)kasan_mem_to_shadow(address + size);
		*shadow = size & KASAN_SHADOW_MASK;
	}
}


/*
 * All functions below always inlined so compiler could
 * perform better optimizations in each of __asan_loadX/__assn_storeX
 * depending on memory access size X.
 */

static __always_inline bool memory_is_poisoned_1(unsigned long addr)
{
	s8 shadow_value = *(s8 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(shadow_value)) {
		s8 last_accessible_byte = addr & KASAN_SHADOW_MASK;
		return unlikely(last_accessible_byte >= shadow_value);
	}

	return false;
}

static __always_inline bool memory_is_poisoned_2(unsigned long addr)
{
	u16 *shadow_addr = (u16 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(*shadow_addr)) {
		if (memory_is_poisoned_1(addr + 1))
			return true;

		if (likely(((addr + 1) & KASAN_SHADOW_MASK) != 0))
			return false;

		return unlikely(*(u8 *)shadow_addr);
	}

	return false;
}

static __always_inline bool memory_is_poisoned_4(unsigned long addr)
{
	u16 *shadow_addr = (u16 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(*shadow_addr)) {
		if (memory_is_poisoned_1(addr + 3))
			return true;

		if (likely(((addr + 3) & KASAN_SHADOW_MASK) >= 3))
			return false;

		return unlikely(*(u8 *)shadow_addr);
	}

	return false;
}

static __always_inline bool memory_is_poisoned_8(unsigned long addr)
{
	u16 *shadow_addr = (u16 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(*shadow_addr)) {
		if (memory_is_poisoned_1(addr + 7))
			return true;

		if (likely(((addr + 7) & KASAN_SHADOW_MASK) >= 7))
			return false;

		return unlikely(*(u8 *)shadow_addr);
	}

	return false;
}

static __always_inline bool memory_is_poisoned_16(unsigned long addr)
{
	u32 *shadow_addr = (u32 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(*shadow_addr)) {
		u16 shadow_first_bytes = *(u16 *)shadow_addr;
		s8 last_byte = (addr + 15) & KASAN_SHADOW_MASK;

		if (unlikely(shadow_first_bytes))
			return true;

		if (likely(!last_byte))
			return false;

		return memory_is_poisoned_1(addr + 15);
	}

	return false;
}

static __always_inline unsigned long bytes_is_zero(const u8 *start,
					size_t size)
{
	while (size) {
		if (unlikely(*start))
			return (unsigned long)start;
		start++;
		size--;
	}

	return 0;
}

static __always_inline unsigned long memory_is_zero(const void *start,
						const void *end)
{
	unsigned int words;
	unsigned long ret;
	unsigned int prefix = (unsigned long)start % 8;

	if (end - start <= 16)
		return bytes_is_zero(start, end - start);

	if (prefix) {
		prefix = 8 - prefix;
		ret = bytes_is_zero(start, prefix);
		if (unlikely(ret))
			return ret;
		start += prefix;
	}

	words = (end - start) / 8;
	while (words) {
		if (unlikely(*(u64 *)start))
			return bytes_is_zero(start, 8);
		start += 8;
		words--;
	}

	return bytes_is_zero(start, (end - start) % 8);
}

static __always_inline bool memory_is_poisoned_n(unsigned long addr,
						size_t size)
{
	unsigned long ret;

	ret = memory_is_zero(kasan_mem_to_shadow((void *)addr),
			kasan_mem_to_shadow((void *)addr + size - 1) + 1);

	if (unlikely(ret)) {
		unsigned long last_byte = addr + size - 1;
		s8 *last_shadow = (s8 *)kasan_mem_to_shadow((void *)last_byte);

		if (unlikely(ret != (unsigned long)last_shadow ||
			((last_byte & KASAN_SHADOW_MASK) >= *last_shadow)))
			return true;
	}
	return false;
}

static __always_inline bool memory_is_poisoned(unsigned long addr, size_t size)
{
	if (__builtin_constant_p(size)) {
		switch (size) {
		case 1:
			return memory_is_poisoned_1(addr);
		case 2:
			return memory_is_poisoned_2(addr);
		case 4:
			return memory_is_poisoned_4(addr);
		case 8:
			return memory_is_poisoned_8(addr);
		case 16:
			return memory_is_poisoned_16(addr);
		default:
			BUILD_BUG();
		}
	}

	return memory_is_poisoned_n(addr, size);
}

static __always_inline void check_memory_region(unsigned long addr,
						size_t size, bool write,
						int skip)
{
	if (unlikely(!kasan_initialized))
		return;

	if (unlikely(size == 0))
		return;

#ifndef CONFIG_KASAN_SHADOW_OFFSET
	if (unlikely(!addr_is_in_mem(addr) || !addr_is_in_mem(addr + size - 1)))
		return;
#else
	if (unlikely((void *)addr <
		kasan_shadow_to_mem((void *)KASAN_SHADOW_START))) {
		struct kasan_access_info info;

		info.access_addr = (void *)addr;
		info.access_size = size;
		info.is_write = write;
		info.ip = _RET_IP_;
		kasan_report_user_access(&info);
		return;
	}
#endif

	if (likely(!memory_is_poisoned(addr, size)))
		return;

	kasan_report(addr, size, write, _RET_IP_, skip);
}

#ifndef CONFIG_KASAN_SHADOW_OFFSET
/*
 * Shadow memory is not defined in the configuration, so
 * we must allocate it dynamically.
 */

static inline size_t lowmem_shadow_size(void)
{
	return ((unsigned long)high_memory - START_VADDR)
		>> KASAN_SHADOW_SCALE_SHIFT;
}

static inline size_t vmalloc_shadow_size(void)
{
#ifdef CONFIG_KASAN_VMALLOC
	return (VMALLOC_END - VMALLOC_START) >> PAGE_SHIFT;
#else
	return 0;
#endif
}

void __init kasan_alloc_shadow(void)
{
	size_t kasan_shadow_size;
	phys_addr_t shadow_phys_addr;

	kasan_shadow_size = lowmem_shadow_size()
		+ vmalloc_shadow_size();

	shadow_phys_addr = memblock_alloc(kasan_shadow_size, PAGE_SIZE);
	if (!shadow_phys_addr) {
		pr_err("Unable to reserve shadow region\n");
		return;
	}

	kasan_shadow_start = (unsigned long)phys_to_virt(shadow_phys_addr);
	kasan_shadow_end = kasan_shadow_start + lowmem_shadow_size();
	kasan_vmalloc_shadow_start = kasan_shadow_end;
	kasan_vmalloc_shadow_end = kasan_vmalloc_shadow_start
		+ vmalloc_shadow_size();

	pr_info("Lowmem shadow start: 0x%lx\n", kasan_shadow_start);
	pr_info("Lowmem shadow end: 0x%lx\n", kasan_shadow_end);
	pr_info("Vmalloc shadow start 0x%lx\n", kasan_vmalloc_shadow_start);
	pr_info("Vmalloc shadow end 0x%lx\n", kasan_vmalloc_shadow_end);
}

void __init kasan_init_shadow(void)
{
	if (kasan_shadow_start) {
		kasan_shadow_offset = kasan_shadow_start
			- (START_VADDR >> KASAN_SHADOW_SCALE_SHIFT);

		kasan_unpoison_shadow((void *)START_VADDR,
				(size_t)(kasan_shadow_start - START_VADDR));
		kasan_poison_shadow((void *)kasan_shadow_start,
			kasan_vmalloc_shadow_end - kasan_shadow_start,
			KASAN_SHADOW_GAP);
		kasan_unpoison_shadow((void *)kasan_vmalloc_shadow_end,
				(size_t)(high_memory - kasan_vmalloc_shadow_end));
#ifdef CONFIG_KASAN_VMALLOC
		kasan_unpoison_shadow((void *)VMALLOC_START,
				(size_t)(VMALLOC_END - VMALLOC_START));
#endif
		kasan_shadow_offset = kasan_shadow_start
			- (START_VADDR >> KASAN_SHADOW_SCALE_SHIFT);

		kasan_initialized = 1;
	}
}

#endif

void __asan_loadN(unsigned long addr, size_t size, int skip);
void __asan_storeN(unsigned long addr, size_t size, int skip);

extern void *__memset(void *, int, __kernel_size_t);
extern void *__memcpy(void *, const void *, __kernel_size_t);
extern void *__memmove(void *, const void *, __kernel_size_t);

#undef memset
void *memset(void *addr, int c, size_t len)
{
	__asan_storeN((unsigned long)addr, len, 1);

	return __memset(addr, c, len);
}

#undef memmove
void *memmove(void *dest, const void *src, size_t len)
{
	__asan_loadN((unsigned long)src, len, 1);
	__asan_storeN((unsigned long)dest, len, 1);

	return __memmove(dest, src, len);
}

#undef memcpy
void *memcpy(void *dest, const void *src, size_t len)
{
	__asan_loadN((unsigned long)src, len, 1);
	__asan_storeN((unsigned long)dest, len, 1);

	return __memcpy(dest, src, len);
}

void kasan_alloc_pages(struct page *page, unsigned int order)
{
	if (likely(!PageHighMem(page)))
		kasan_unpoison_shadow(page_address(page), PAGE_SIZE << order);
}

void kasan_free_pages(struct page *page, unsigned int order)
{
	if (likely(!PageHighMem(page)))
		kasan_poison_shadow(page_address(page),
				PAGE_SIZE << order,
				KASAN_FREE_PAGE);
}

void kasan_poison_slab(struct page *page)
{
	kasan_poison_shadow(page_address(page),
			PAGE_SIZE << compound_order(page),
			KASAN_KMALLOC_REDZONE);
}

void kasan_unpoison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_unpoison_shadow(object, cache->object_size);
}

void kasan_poison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_poison_shadow(object,
			round_up(cache->object_size, KASAN_SHADOW_SCALE_SIZE),
			KASAN_KMALLOC_REDZONE);
}

void kasan_slab_alloc(struct kmem_cache *cache, void *object)
{
	kasan_kmalloc(cache, object, cache->object_size);
}

void kasan_slab_free(struct kmem_cache *cache, void *object)
{
	unsigned long size = cache->object_size;
	unsigned long rounded_up_size = round_up(size, KASAN_SHADOW_SCALE_SIZE);

	/* RCU slabs could be legally used after free within the RCU period */
	if (unlikely(cache->flags & SLAB_DESTROY_BY_RCU))
		return;

	kasan_poison_shadow(object, rounded_up_size, KASAN_KMALLOC_FREE);
}

void kasan_kmalloc(struct kmem_cache *cache, const void *object, size_t size)
{
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (unlikely(object == NULL))
		return;

	redzone_start = round_up((unsigned long)(object + size),
				KASAN_SHADOW_SCALE_SIZE);
	redzone_end = round_up((unsigned long)object + cache->object_size,
				KASAN_SHADOW_SCALE_SIZE);

	kasan_unpoison_shadow(object, size);
	kasan_poison_shadow((void *)redzone_start, redzone_end - redzone_start,
		KASAN_KMALLOC_REDZONE);
}
EXPORT_SYMBOL(kasan_kmalloc);

void kasan_kmalloc_large(const void *ptr, size_t size)
{
	struct page *page;
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (unlikely(ptr == NULL))
		return;

	page = virt_to_page(ptr);
	redzone_start = round_up((unsigned long)(ptr + size),
				KASAN_SHADOW_SCALE_SIZE);
	redzone_end = (unsigned long)ptr + (PAGE_SIZE << compound_order(page));

	kasan_unpoison_shadow(ptr, size);
	kasan_poison_shadow((void *)redzone_start, redzone_end - redzone_start,
		KASAN_PAGE_REDZONE);
}

void kasan_krealloc(const void *object, size_t size)
{
	struct page *page;

	if (unlikely(object == ZERO_SIZE_PTR))
		return;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page)))
		kasan_kmalloc_large(object, size);
	else
		kasan_kmalloc(page->slab_cache, object, size);
}

void kasan_kfree(void *ptr)
{
	struct page *page;

	page = virt_to_head_page(ptr);

	if (unlikely(!PageSlab(page)))
		kasan_poison_shadow(ptr, PAGE_SIZE << compound_order(page),
				KASAN_FREE_PAGE);
	else
		kasan_slab_free(page->slab_cache, ptr);
}

void kasan_kfree_large(const void *ptr)
{
	struct page *page = virt_to_page(ptr);

	kasan_poison_shadow(ptr, PAGE_SIZE << compound_order(page),
			KASAN_FREE_PAGE);
}

int kasan_module_alloc(void *addr, size_t size)
{
	void *ret;
	size_t shadow_size;
	unsigned long shadow_start;

	shadow_start = (unsigned long)kasan_mem_to_shadow(addr);
	shadow_size = round_up(size >> KASAN_SHADOW_SCALE_SHIFT,
			PAGE_SIZE);

	if (WARN_ON(!PAGE_ALIGNED(shadow_start)))
		return -EINVAL;

	ret = __vmalloc_node_range(shadow_size, 1, shadow_start,
			shadow_start + shadow_size,
			GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
			PAGE_KERNEL, VM_NO_GUARD, NUMA_NO_NODE,
			__builtin_return_address(0));

	if (ret) {
		find_vm_area(addr)->flags |= VM_KASAN;
		return 0;
	}

	return -ENOMEM;
}

void kasan_free_shadow(const struct vm_struct *vm)
{
	if (vm->flags & VM_KASAN)
		vfree(kasan_mem_to_shadow(vm->addr));
}

#ifdef CONFIG_KASAN_VMALLOC

void kasan_vmalloc(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr) ||
		!is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	kasan_unpoison_shadow((void *)addr, size - PAGE_SIZE);
	kasan_poison_shadow((void *)addr + size - PAGE_SIZE,
		PAGE_SIZE, KASAN_VMALLOC_REDZONE);
}

void kasan_vmalloc_nopageguard(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr)
		|| !is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	kasan_unpoison_shadow((void *)addr, size);
}

void kasan_vfree(unsigned long addr, size_t size)
{
	if (!is_vmalloc_addr((void *)addr)
		|| !is_vmalloc_addr((void *)(addr + size - 1)))
		return;

	kasan_poison_shadow((void *)addr, size, KASAN_VMALLOC_FREE);
}

#endif /* CONFIG_KASAN_VMALLOC */

static void register_global(struct kasan_global *global)
{
	size_t aligned_size = round_up(global->size, KASAN_SHADOW_SCALE_SIZE);

	kasan_unpoison_shadow(global->beg, global->size);

	kasan_poison_shadow(global->beg + aligned_size,
		global->size_with_redzone - aligned_size,
		KASAN_GLOBAL_REDZONE);
}

void __asan_register_globals(struct kasan_global *globals, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		register_global(&globals[i]);
}
EXPORT_SYMBOL(__asan_register_globals);

void __asan_unregister_globals(struct kasan_global *globals, size_t size)
{
}
EXPORT_SYMBOL(__asan_unregister_globals);

#ifdef CONFIG_KASAN_GLOBALS
void kasan_module_load(void *mod, size_t size)
{
	if (mod)
		kasan_unpoison_shadow(mod, size);
}
#endif

#define DEFINE_ASAN_LOAD_STORE(size)				\
	void __asan_load##size(unsigned long addr)		\
	{							\
		check_memory_region(addr, size, false, 0);		\
	}							\
	EXPORT_SYMBOL(__asan_load##size);			\
	__alias(__asan_load##size)				\
	void __asan_load##size##_noabort(unsigned long);	\
	EXPORT_SYMBOL(__asan_load##size##_noabort);		\
	void __asan_store##size(unsigned long addr)		\
	{							\
		check_memory_region(addr, size, true, 0);		\
	}							\
	EXPORT_SYMBOL(__asan_store##size);			\
	__alias(__asan_store##size)				\
	void __asan_store##size##_noabort(unsigned long);	\
	EXPORT_SYMBOL(__asan_store##size##_noabort)

DEFINE_ASAN_LOAD_STORE(1);
DEFINE_ASAN_LOAD_STORE(2);
DEFINE_ASAN_LOAD_STORE(4);
DEFINE_ASAN_LOAD_STORE(8);
DEFINE_ASAN_LOAD_STORE(16);

void __asan_loadN(unsigned long addr, size_t size, int skip)
{
	check_memory_region(addr, size, false, skip);
}
EXPORT_SYMBOL(__asan_loadN);

__alias(__asan_loadN)
void __asan_loadN_noabort(unsigned long, size_t);
EXPORT_SYMBOL(__asan_loadN_noabort);

void __asan_storeN(unsigned long addr, size_t size, int skip)
{
	check_memory_region(addr, size, true, skip);
}
EXPORT_SYMBOL(__asan_storeN);

__alias(__asan_storeN)
void __asan_storeN_noabort(unsigned long, size_t);
EXPORT_SYMBOL(__asan_storeN_noabort);

/* to shut up compiler complaints */
void __asan_handle_no_return(void) {}
EXPORT_SYMBOL(__asan_handle_no_return);

unsigned long __asan_get_shadow_ptr(void)
{
	return kasan_shadow_offset;
}
EXPORT_SYMBOL(__asan_get_shadow_ptr);

#ifdef CONFIG_MEMORY_HOTPLUG
static int kasan_mem_notifier(struct notifier_block *nb,
			unsigned long action, void *data)
{
	return (action == MEM_GOING_ONLINE) ? NOTIFY_BAD : NOTIFY_OK;
}

static int __init kasan_memhotplug_init(void)
{
	pr_err("WARNING: KASan doesn't support memory hot-add\n");
	pr_err("Memory hot-add will be disabled\n");

	hotplug_memory_notifier(kasan_mem_notifier, 0);

	return 0;
}

module_init(kasan_memhotplug_init);
#endif
