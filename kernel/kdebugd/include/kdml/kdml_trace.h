/*
 * kdml_trace.h
 *
 * Copyright (C) 2014 Samsung Electronics
 * Created by Himanshu Maithani (himanshu.m@samsung.com)
 *
 * NOTE:
 *
 */

#if !defined(_KDML_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _KDML_TRACE_H
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kdml
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <trace/events/gfpflags.h>

/* modify tracepoint to use call_site */
TRACE_EVENT(mm_page_alloc_kdml,

	TP_PROTO(struct page *page, unsigned int order,
			gfp_t gfp_flags, unsigned long call_site),

	TP_ARGS(page, order, gfp_flags, call_site),

	TP_STRUCT__entry(
		__field(	struct page *,	page		)
		__field(	unsigned int,	order		)
		__field(	gfp_t,		gfp_flags	)
		__field(	unsigned long,	call_site	)
	),

	TP_fast_assign(
		__entry->page		= page;
		__entry->order		= order;
		__entry->gfp_flags	= gfp_flags;
		__entry->call_site	= call_site;
	),

	TP_printk("page=%p pfn=%lu order=%d call_site=%lx gfp_flags=%s",
		__entry->page,
		__entry->page ? page_to_pfn(__entry->page) : 0,
		__entry->order,
		__entry->call_site,
		show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(vmalloc_kdml,

	TP_PROTO(void *addr, unsigned long real_size,
			gfp_t gfp_flags, const void *call_site,
			enum kdml_vmalloc_trace_type vmalloc_type),

	TP_ARGS(addr, real_size, gfp_flags, call_site, vmalloc_type),

	TP_STRUCT__entry(
		__field(void *, addr)
		__field(unsigned long, real_size)
		__field(gfp_t, gfp_flags)
		__field(const void *, call_site)
		__field(enum kdml_vmalloc_trace_type, vmalloc_type)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->real_size = real_size;
		__entry->gfp_flags = gfp_flags;
		__entry->call_site = call_site;
		__entry->vmalloc_type = vmalloc_type;
	),

	TP_printk("addr=%p size=%lx call_site=%p gfp_flags=%s alloc=%s",
		__entry->addr,
		__entry->real_size,
		__entry->call_site,
		show_gfp_flags(__entry->gfp_flags),
		__entry->vmalloc_type ? "map" : "vmalloc")
);

TRACE_EVENT(vfree_kdml,

	TP_PROTO(unsigned long call_site, const void *ptr),

	TP_ARGS(call_site, ptr),

	TP_STRUCT__entry(
		__field(unsigned long, call_site)
		__field(const void *, ptr)
	),

	TP_fast_assign(
		__entry->call_site = call_site;
		__entry->ptr = ptr;
	),

	TP_printk("call_site=%lx ptr=%p", __entry->call_site, __entry->ptr)
);

#endif /* _KDML_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
/* path till include is already exported */
#define TRACE_INCLUDE_PATH kdml
#define TRACE_INCLUDE_FILE kdml_trace
#include <trace/define_trace.h>
