/*
 * This file contains error reporting code.
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

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/kasan.h>
#include <asm/traps.h>
#include <asm/memory.h>

#include <asm/sections.h>
#include <linux/hashtable.h>

#include "kasan.h"
#include "../slab.h"

/* Shadow layout customization. */
#define SHADOW_BYTES_PER_BLOCK 1
#define SHADOW_BLOCKS_PER_ROW 16
#define SHADOW_BYTES_PER_ROW (SHADOW_BLOCKS_PER_ROW * SHADOW_BYTES_PER_BLOCK)
#define SHADOW_ROWS_AROUND_ADDR 2

#define KASAN_HASH_BIT 20
static DEFINE_HASHTABLE(stacktrace_ht, KASAN_HASH_BIT);
DEFINE_SPINLOCK(kasan_hash_lock);
#define HASH_LIM (1 << KASAN_HASH_BIT)

enum {
	KASAN_ALLOC_BT,
	KASAN_FREE_BT,
	KASAN_FAULT_BT,
	KASAN_MAX_BT
};
struct trace_hash {
	struct hlist_node hentry;
	unsigned int nr_entries_access;
	unsigned int nr_entries_alloc;
	unsigned int nr_entries_free;
	unsigned long entries_alloc[TRACK_ADDRS_COUNT];
	unsigned long entries_free[TRACK_ADDRS_COUNT];
	unsigned long entries_access[TRACK_ADDRS_COUNT];
	size_t access_size;
};

static const void *find_first_bad_addr(const void *addr, size_t size)
{
	u8 shadow_val = *(u8 *)kasan_mem_to_shadow(addr);
	const void *first_bad_addr = addr;

	while (!shadow_val && first_bad_addr < addr + size) {
		first_bad_addr += KASAN_SHADOW_SCALE_SIZE;
		shadow_val = *(u8 *)kasan_mem_to_shadow(first_bad_addr);
	}
	return first_bad_addr;
}

static void print_error_description(struct kasan_access_info *info)
{
	const char *bug_type = "unknown crash";
	u8 shadow_val;

	info->first_bad_addr = find_first_bad_addr(info->access_addr,
						info->access_size);

	shadow_val = *(u8 *)kasan_mem_to_shadow(info->first_bad_addr);

	switch (shadow_val) {
	case KASAN_FREE_PAGE:
	case KASAN_KMALLOC_FREE:
	case KASAN_VMALLOC_FREE:
		bug_type = "use after free";
		break;
	case KASAN_PAGE_REDZONE:
	case KASAN_KMALLOC_REDZONE:
	case KASAN_VMALLOC_REDZONE:
	case KASAN_GLOBAL_REDZONE:
	case 0 ... KASAN_SHADOW_SCALE_SIZE - 1:
		bug_type = "out of bounds access";
		break;
	case KASAN_STACK_LEFT:
	case KASAN_STACK_MID:
	case KASAN_STACK_RIGHT:
	case KASAN_STACK_PARTIAL:
		bug_type = "out of bounds on stack";
		break;
	}

	pr_err("BUG: KASan: %s in %pS at addr %p\n",
		bug_type, (void *)info->ip,
		info->access_addr);
	pr_err("%s of size %zu by task %s/%d\n",
		info->is_write ? "Write" : "Read",
		info->access_size, current->comm, task_pid_nr(current));
}

static inline bool kernel_or_module_addr(const void *addr)
{
	return (addr >= (void *)_stext && addr < (void *)_end)
		|| (addr >= (void *)MODULES_VADDR
			&& addr < (void *)MODULES_END);
}

static inline bool init_task_stack_addr(const void *addr)
{
	return addr >= (void *)&init_thread_union.stack &&
		(addr <= (void *)&init_thread_union.stack +
			sizeof(init_thread_union.stack));
}

static inline unsigned int kasan_hash(unsigned long int entry)
{
	unsigned int hash = ((unsigned long int)_etext - entry) % HASH_LIM;
	return hash;
}

static int add_trace_entry(struct kasan_access_info *info,
		struct stack_trace trace[KASAN_MAX_BT], struct trace_hash *node)
{
	u32 hash;
	int i, match_found = 0;

	hash = kasan_hash(trace[KASAN_FAULT_BT].entries[0]);
	spin_lock(&kasan_hash_lock);
	hash_for_each_possible(stacktrace_ht, node, hentry, hash) {
		if (node->access_size != info->access_size)
			continue;
		do {
			if (node->nr_entries_alloc != trace[KASAN_ALLOC_BT].nr_entries ||
					node->nr_entries_free != trace[KASAN_FREE_BT].nr_entries ||
					node->nr_entries_access != trace[KASAN_FAULT_BT].nr_entries)
				break;
			for (i = 0; i < trace[KASAN_ALLOC_BT].nr_entries; i++)
				if (trace[KASAN_ALLOC_BT].entries[i] != node->entries_alloc[i])
					break;
			if (i != trace[KASAN_ALLOC_BT].nr_entries)
				break;
			for (i = 0; i < trace[KASAN_FREE_BT].nr_entries; i++)
				if (trace[KASAN_FREE_BT].entries[i] != node->entries_free[i])
					break;
			if (i != trace[KASAN_FREE_BT].nr_entries)
				break;
			for (i = 0; i < trace[KASAN_FAULT_BT].nr_entries; i++)
				if (trace[KASAN_FAULT_BT].entries[i] != node->entries_access[i])
					break;
			if (i != trace[KASAN_FAULT_BT].nr_entries)
				break;
			match_found = 1;
		} while (0);
		if (match_found) {
			spin_unlock(&kasan_hash_lock);
			return 0;
		}
	}
	spin_unlock(&kasan_hash_lock);
	node = kzalloc(sizeof(struct trace_hash), GFP_KERNEL);
	spin_lock(&kasan_hash_lock);
	if (node) {
		node->access_size = info->access_size;
		node->nr_entries_alloc = trace[KASAN_ALLOC_BT].nr_entries;
		node->nr_entries_free = trace[KASAN_FREE_BT].nr_entries;
		node->nr_entries_access = trace[KASAN_FAULT_BT].nr_entries;
		for (i = 0; i < trace[KASAN_ALLOC_BT].nr_entries; i++)
			node->entries_alloc[i] = trace[KASAN_ALLOC_BT].entries[i];
		for (i = 0; i < trace[KASAN_FREE_BT].nr_entries; i++)
			node->entries_free[i] = trace[KASAN_FREE_BT].entries[i];
		for (i = 0; i < trace[KASAN_FAULT_BT].nr_entries; i++)
			node->entries_access[i] = trace[KASAN_FAULT_BT].entries[i];
		hash_add(stacktrace_ht, &node->hentry, hash);
	}
	spin_unlock(&kasan_hash_lock);
	return 1;
}

static int kasan_find_object(struct page *page, struct kmem_cache **cache,
		void **object, const void *addr){
	void *last_object;

	if (PageSlab(page)) {
		*cache = page->slab_cache;

		*object = virt_to_obj(*cache, page_address(page), addr);
		last_object = page_address(page) +
			page->objects * (*cache)->size;

		if (unlikely((*object) > last_object))
			*object = last_object; /* we hit into padding */
		return 1;
	}
	return 0;
}

static int kasan_save_kernel_stack(struct kasan_access_info *info, int skip)
{
	struct stack_trace trace[KASAN_MAX_BT];
	unsigned long stack_trace[TRACK_ADDRS_COUNT], stack_trace_alloc[TRACK_ADDRS_COUNT],
		      stack_trace_free[TRACK_ADDRS_COUNT];
	struct trace_hash *node = NULL;
	int i;
	const void *addr = info->access_addr;
	struct page *page;
	struct kmem_cache *cache;
	void *object;

	trace[0].entries = stack_trace_alloc;
	trace[1].entries = stack_trace_free;
	trace[2].entries = stack_trace;
	for (i = 0; i < KASAN_MAX_BT; i++) {
		trace[i].nr_entries = 0;
		trace[i].max_entries = TRACK_ADDRS_COUNT;
		trace[i].skip = skip + 2;
	}
	if ((addr >= (void *)PAGE_OFFSET) &&
			(addr < high_memory)) {
		page = virt_to_head_page(addr);

		if (kasan_find_object(page, &cache, &object, addr) &&
				(cache->flags & SLAB_STORE_USER)) {
			kasan_get_track(cache, object, 0, &trace[KASAN_ALLOC_BT]);
			kasan_get_track(cache, object, 1, &trace[KASAN_FREE_BT]);
		}
	}

	kasan_disable_current();
	save_stack_trace_tsk(current, &trace[KASAN_FAULT_BT]);
	kasan_enable_current();
	return add_trace_entry(info, trace, node);
}

static void print_address_description(struct kasan_access_info *info)
{
	const void *addr = info->access_addr;
	struct page *page;
	struct kmem_cache *cache;
	void *object;

	if ((addr >= (void *)PAGE_OFFSET) &&
			(addr < high_memory)) {
		page = virt_to_head_page(addr);

		if (kasan_find_object(page, &cache, &object, addr)) {
			object_err(cache, page, object,
				"kasan: bad access detected");
			return;
		}
		dump_page(page, "kasan: bad access detected");
	}

	if (kernel_or_module_addr(addr)) {
		if (!init_task_stack_addr(addr))
			pr_err("Address belongs to variable %pS\n", addr);
	}
	dump_stack();
}

static bool row_is_guilty(const void *row, const void *guilty)
{
	return (row <= guilty) && (guilty < row + SHADOW_BYTES_PER_ROW);
}

static int shadow_pointer_offset(const void *row, const void *shadow)
{
	/* The length of ">ff00ff00ff00ff00: " is
	 *    3 + (BITS_PER_LONG/8)*2 chars.
	 */
	return 3 + (BITS_PER_LONG/8)*2 + (shadow - row)*2 +
		(shadow - row) / SHADOW_BYTES_PER_BLOCK + 1;
}

static void print_shadow_for_address(const void *addr)
{
	int i;
	const void *shadow = kasan_mem_to_shadow(addr);
	const void *shadow_row;

	shadow_row = (void *)round_down((unsigned long)shadow,
					SHADOW_BYTES_PER_ROW)
		- SHADOW_ROWS_AROUND_ADDR * SHADOW_BYTES_PER_ROW;

	pr_err("Memory state around the buggy address:\n");

	for (i = -SHADOW_ROWS_AROUND_ADDR; i <= SHADOW_ROWS_AROUND_ADDR; i++) {
		const void *kaddr = kasan_shadow_to_mem(shadow_row);
		char buffer[4 + (BITS_PER_LONG/8)*2];

		snprintf(buffer, sizeof(buffer),
			(i == 0) ? ">%p: " : " %p: ", kaddr);

		kasan_disable_current();
		print_hex_dump(KERN_ERR, buffer,
			DUMP_PREFIX_NONE, SHADOW_BYTES_PER_ROW, 1,
			shadow_row, SHADOW_BYTES_PER_ROW, 0);
		kasan_enable_current();

		if (row_is_guilty(shadow_row, shadow))
			pr_err("%*c\n",
				shadow_pointer_offset(shadow_row, shadow),
				'^');

		shadow_row += SHADOW_BYTES_PER_ROW;
	}
}

static DEFINE_SPINLOCK(report_lock);

void kasan_report_error(struct kasan_access_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&report_lock, flags);
	pr_err("================================="
		"=================================\n");
	print_error_description(info);
	print_address_description(info);
	print_shadow_for_address(info->first_bad_addr);
	pr_err("================================="
		"=================================\n");
	spin_unlock_irqrestore(&report_lock, flags);
}

void kasan_report_user_access(struct kasan_access_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&report_lock, flags);
	pr_err("================================="
		"=================================\n");
	pr_err("BUG: KASan: user-memory-access on address %p\n",
		info->access_addr);
	pr_err("%s of size %zu by task %s/%d\n",
		info->is_write ? "Write" : "Read",
		info->access_size, current->comm, task_pid_nr(current));
	dump_stack();
	pr_err("================================="
		"=================================\n");
	spin_unlock_irqrestore(&report_lock, flags);
}

void kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip, int skip)
{
	struct kasan_access_info info;
	if (likely(!kasan_enabled()))
		return;

	info.access_addr = (void *)addr;
	info.access_size = size;
	info.is_write = is_write;
	info.ip = ip;
	if (kasan_save_kernel_stack(&info, skip))
		kasan_report_error(&info);
}


#define DEFINE_ASAN_REPORT_LOAD(size)                     \
void __asan_report_load##size##_noabort(unsigned long addr) \
{                                                         \
	kasan_report(addr, size, false, _RET_IP_, 0);	  \
}                                                         \
EXPORT_SYMBOL(__asan_report_load##size##_noabort)

#define DEFINE_ASAN_REPORT_STORE(size)                     \
void __asan_report_store##size##_noabort(unsigned long addr) \
{                                                          \
	kasan_report(addr, size, true, _RET_IP_, 0);	   \
}                                                          \
EXPORT_SYMBOL(__asan_report_store##size##_noabort)

DEFINE_ASAN_REPORT_LOAD(1);
DEFINE_ASAN_REPORT_LOAD(2);
DEFINE_ASAN_REPORT_LOAD(4);
DEFINE_ASAN_REPORT_LOAD(8);
DEFINE_ASAN_REPORT_LOAD(16);
DEFINE_ASAN_REPORT_STORE(1);
DEFINE_ASAN_REPORT_STORE(2);
DEFINE_ASAN_REPORT_STORE(4);
DEFINE_ASAN_REPORT_STORE(8);
DEFINE_ASAN_REPORT_STORE(16);

void __asan_report_load_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, false, _RET_IP_, 0);
}
EXPORT_SYMBOL(__asan_report_load_n_noabort);

void __asan_report_store_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, true, _RET_IP_, 0);
}
EXPORT_SYMBOL(__asan_report_store_n_noabort);
