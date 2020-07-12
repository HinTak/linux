#ifndef __KDBUS_DEBUG_H
#define __KDBUS_DEBUG_H

struct kdbus_debug;

#ifdef CONFIG_KDBUS_DEBUG
enum {
	KDBUS_STAT_CONN_NUM,
} typedef kdbus_stat_t;
void kdbus_debug_bus_init(struct kdbus_bus *bus);
void kdbus_debug_bus_end(struct kdbus_bus *bus);
void kdbus_debug_conn_init(struct kdbus_conn *conn);
void kdbus_debug_conn_end(struct kdbus_conn *conn);
void kdbus_debug_conn_show(struct kdbus_conn *conn);
int  kdbus_debug_log(const char *fmt, ...);
void kdbus_debug_stat_show(struct kdbus_bus *bus, kdbus_stat_t type);
#else
#define kdbus_debug_bus_init(b)
#define kdbus_debug_bus_end(b)
#define kdbus_debug_conn_init(c)
#define kdbus_debug_conn_end(c)
#define kdbus_debug_conn_show(c);
#define kdbus_debug_log(fmt...)
#define kdbus_debug_stat_show(b, t);
#endif /* CONFIG_KDBUS_DEBUG */

#ifdef CONFIG_KDBUS_DEBUG_TRACE
/*
 * KDBus Trace infrastructure
 */
enum {
	_TRACE_BIN_UNTOUCHED = 0,
	KDBUS_TRACE_SEND_SYNC,
	KDBUS_TRACE_SEND_ASYNC,
	KDBUS_TRACE_SEND_REPLY,
	KDBUS_TRACE_RECV,
	KDBUS_TRACE_BROADCAST,
	KDBUS_TRACE_POOL_INSERTED,
} typedef kdbus_trace_t;

void kdbus_trace_add(struct kdbus_conn *conn, kdbus_trace_t act, ...);
#else
#define kdbus_trace_add(c, a, ...)
#endif /* CONFIG_KDBUS_DEBUG_TRACE */
#endif
