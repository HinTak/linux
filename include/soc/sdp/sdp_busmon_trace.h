

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sdp_busmon

#if !defined(__TRACE_SDP_BUSMON_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __TRACE_SDP_BUSMON_H__

#include <linux/tracepoint.h>

TRACE_EVENT(busmon_update_event,
	TP_PROTO(char* name, u32 bw_w, u32 bw_r, u32 lmax_w, u32 lmax_r),
	TP_ARGS(name, bw_w, bw_r, lmax_w, lmax_r),
	TP_STRUCT__entry(
		__field(char*, name)
		__field(u32, bw_w)
		__field(u32, bw_r)
		__field(u32, lmax_w)
		__field(u32, lmax_r)
	),
	TP_fast_assign(
		__entry->name = name;
		__entry->bw_w = bw_w;
		__entry->bw_r = bw_r;
		__entry->lmax_w = lmax_w;
		__entry->lmax_r = lmax_r;
	),
	TP_printk("dev=%s bw_w=%u bw_r=%u lmax_w=%u lmax_r=%u",	
		__entry->name, __entry->bw_w, __entry->bw_r, __entry->lmax_w, __entry->lmax_r)
);

void* sdp_busmon_get_by_name(char *name, int n);
int   sdp_busmon_set_trace(void *dev, bool on);
#endif

#define TRACE_INCLUDE_PATH soc/sdp/
#define TRACE_INCLUDE_FILE sdp_busmon_trace
#include <trace/define_trace.h>


