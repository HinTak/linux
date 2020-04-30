#include <linux/capability.h>
#include <linux/cgroup.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/rwsem.h>
#include <linux/debugfs.h>
#include <linux/plist.h>
#include <linux/ring_buffer.h>

#include "bus.h"
#include "connection.h"
#include "domain.h"
#include "endpoint.h"
#include "handle.h"
#include "item.h"
#include "match.h"
#include "message.h"
#include "metadata.h"
#include "names.h"
#include "policy.h"
#include "reply.h"
#include "debug.h"

struct kdbus_debug {
	struct rw_semaphore rwlock;
	struct dentry *debug_dentry;
	void *trace;
	u8 trace_iter;
};

#define kdbus_debug_print(m, x...) \
 do {                              \
	if (m)                         \
		seq_printf(m, x);          \
	else                           \
		kdbus_debug_log(x);\
 } while (0)

static struct ring_buffer *kdbus_debug_rb;

#ifdef CONFIG_KDBUS_DEBUG_TRACE
#define KDBUS_TRACE_MAX	CONFIG_KDBUS_DEBUG_NUM_TRACES
#define KDBUS_INFINITE_TIMEOUT_NS   0x3FFFFFFFFFFFFFFFLLU

struct kdbus_trace_dump {
	char dst_comm[TASK_COMM_LEN];
	u32 data32[2];
	u64 data64[3];
};

struct kdbus_trace {
	kdbus_trace_t type;
	u64 timestamp;
	void *data;
};

static const char *kdbus_trace_type_str[] = {
	[KDBUS_TRACE_SEND_SYNC]		= "SYNC",
	[KDBUS_TRACE_SEND_ASYNC]	= "ASYNC",
	[KDBUS_TRACE_SEND_REPLY]	= "REPLY",
	[KDBUS_TRACE_BROADCAST]		= "SIGNAL",
	[KDBUS_TRACE_RECV]			= "RECV",
	[KDBUS_TRACE_POOL_INSERTED] = "INSERTED",
};

#define trace_bin(tr, i)	(((struct kdbus_trace *)tr)[i])

static struct kdbus_trace *kdbus_trace_pickup_bin(struct kdbus_debug* dbg)
{
	struct kdbus_trace *cand;

	cand = &trace_bin(dbg->trace, dbg->trace_iter);
	dbg->trace_iter = (dbg->trace_iter + 1) % KDBUS_TRACE_MAX;
	cand->timestamp = local_clock();
	if (cand->data)
		memset(cand->data, 0, sizeof(struct kdbus_trace_dump));
	else
		cand->data = kzalloc(sizeof(struct kdbus_trace_dump),
							GFP_KERNEL);

	return cand;
}

void kdbus_trace_add(struct kdbus_conn *conn, kdbus_trace_t act,
						  ...)
{
	struct kdbus_debug *dbg;
	struct kdbus_trace *bin;
	struct kdbus_trace_dump *dump = NULL;
	struct kdbus_conn *dst;
	struct kdbus_msg *msg;
	struct kdbus_queue_entry *entry; 
	bool sync;
	va_list ap;

	if (!conn)
		return;

	dbg = conn->dbg;

	down_write(&dbg->rwlock);
	bin = kdbus_trace_pickup_bin(dbg);
	bin->type = act;
	dump = bin->data;

	va_start(ap, act);
	switch (act){
	case KDBUS_TRACE_SEND_SYNC :
	case KDBUS_TRACE_SEND_ASYNC :
	case KDBUS_TRACE_SEND_REPLY :
		dst = va_arg(ap, struct kdbus_conn *);
		msg = va_arg(ap, struct kdbus_msg *);
		strncpy(dump->dst_comm, kdbus_metadata_pid_comm(dst),
				TASK_COMM_LEN);
		dump->data32[0] = pid_vnr(dst->pid);
		dump->data64[0] = dst->id;
		dump->data64[1] = msg->cookie;
		dump->data64[2] = msg->cookie_reply;
		break;
	case KDBUS_TRACE_RECV:
		entry = va_arg(ap, struct kdbus_queue_entry *);
		if (entry->reply) {
			dst = entry->reply->reply_dst;
			strncpy(dump->dst_comm,	kdbus_metadata_pid_comm(dst),
					TASK_COMM_LEN);
			dump->data32[0] = pid_vnr(dst->pid);
			dump->data64[0] = dst->id;
			dump->data64[1] = entry->reply->cookie;
		} else {
			struct kdbus_msg _msg;
			struct kdbus_pool *pool = va_arg(ap, struct kdbus_pool *);
			loff_t off = kdbus_pool_slice_offset(entry->slice);

			__vfs_read(*((struct file **)pool), (char *)&_msg,
					sizeof(_msg), &off);
			dump->dst_comm[0] = '\0';
			dump->data32[0] = 0;
			dump->data64[0] = _msg.src_id;
			dump->data64[1] = _msg.cookie;
			dump->data64[2] = _msg.cookie_reply;
		}
		break;
	case KDBUS_TRACE_BROADCAST:
		msg = va_arg(ap, struct kdbus_msg *);
		dump->data64[0] = msg->cookie; 
		break;
	case KDBUS_TRACE_POOL_INSERTED:
		msg = va_arg(ap, struct kdbus_msg *);
		get_task_comm(dump->dst_comm, current->group_leader);
		dump->data32[0] = current->pid;
		dump->data32[1] = msg->flags & KDBUS_MSG_EXPECT_REPLY;
		dump->data64[0] = msg->src_id;
		dump->data64[1] = msg->cookie;
		dump->data64[2] = msg->cookie_reply;
		break;
	default:
		break;
	}
	va_end(ap);
	up_write(&dbg->rwlock);
}

static void show_item(struct kdbus_trace *t, struct seq_file *s)
{
	struct kdbus_trace_dump *d;
	u64 sec;
	unsigned long msec;
	char head[64];

	sec = t->timestamp;
	msec = do_div(sec, NSEC_PER_SEC);
	msec /= NSEC_PER_MSEC;

	snprintf(head, sizeof(head), " %5llu.%03lu %8s",
			sec, msec, kdbus_trace_type_str[t->type]);

	d = (struct kdbus_trace_dump *)t->data;

	switch (t->type) {
	case KDBUS_TRACE_SEND_SYNC:
	case KDBUS_TRACE_SEND_ASYNC:
		if (d->data64[2] < KDBUS_INFINITE_TIMEOUT_NS) {
			sec = d->data64[2];
			msec = do_div(sec, NSEC_PER_SEC);
			msec /= NSEC_PER_MSEC;
		} else {
			sec = -1;
			msec = 0;
		}
		kdbus_debug_print(s, "%s %10llu t: %5lld.%03lu %6llu %16s %5d\n",
		head, d->data64[1], sec, msec, d->data64[0], d->dst_comm, d->data32[0]);
		break;
	case KDBUS_TRACE_SEND_REPLY:
		kdbus_debug_print(s, "%s %10llu r:%10llu %6llu %16s %5d\n",
		head, d->data64[1], d->data64[2], d->data64[0],
		d->dst_comm, d->data32[0]);
		break;
	case KDBUS_TRACE_RECV:
		if (d->data64[0] == 0) {
			kdbus_debug_print(s, "%s %10llu -:%10llu %6llu %16s %5s\n",
				head, d->data64[1], d->data64[2], d->data64[0], "kernel", "-");
		} else {
			if (d->data32[0])
				kdbus_debug_print(s, "%s %10llu -:%10s %6llu %16s %5d\n",
				head, d->data64[1], "-", d->data64[0],
				d->dst_comm, d->data32[0]);
			else
				kdbus_debug_print(s, "%s %10llu r:%10llu %6llu %16s %5s\n",
				head, d->data64[1], d->data64[2], d->data64[0],
				"<Undiscovered>", "-");
		}
		break;
	case KDBUS_TRACE_BROADCAST:
		kdbus_debug_print(s, "%s %10llu\n", head, d->data64[0]);
		break;
	case KDBUS_TRACE_POOL_INSERTED:
		if (d->data32[1]) {
			if (d->data64[2] < KDBUS_INFINITE_TIMEOUT_NS) {
				sec = d->data64[2];
				msec = do_div(sec, NSEC_PER_SEC);
				msec /= NSEC_PER_MSEC;
			} else {
				sec = -1;
				msec = 0;
			}
			kdbus_debug_print(s, "%s %10llu t: %5lld.%03lu %6llu %16s %5d\n",
				head, d->data64[1], sec, msec, d->data64[0],
				d->dst_comm, d->data32[0]);
		} else {
			kdbus_debug_print(s, "%s %10llu r:%10llu %6llu %16s %5d\n",
				head, d->data64[1], d->data64[2], d->data64[0],
				d->dst_comm, d->data32[0]);
		}
		break;
	default:
		break;
	}
}

static void __kdbus_trace_show(struct kdbus_conn *conn, struct seq_file *s)
{
	struct kdbus_debug *dbg;
	struct kdbus_trace *_t, *first, *last;
	int i, trace_size;

	dbg = conn->dbg;
	if (!dbg)
		return;

	down_read(&dbg->rwlock);

	/* header */
	kdbus_debug_print(s,
	"+------------------------------------------------------------------------\n");
	kdbus_debug_print(s,
	"| Time    |   Type |   MsgNum |r: ReplyNum |______Peer_connection_info___\n");
	kdbus_debug_print(s,
	"| stamp   |        |          |t: Deadline |  ID  |       PGcomm   | Pid \n");
	kdbus_debug_print(s,
	"+------------------------------------------------------------------------\n");

	last = &trace_bin(dbg->trace, KDBUS_TRACE_MAX -1);
	if (!dbg->trace_iter && !last->type) {
		kdbus_debug_print(s, "***  Trace bin is empty  ***\n");
		up_read(&dbg->rwlock);
		return;
	}
	first = &trace_bin(dbg->trace, 0);

	_t = &trace_bin(dbg->trace, dbg->trace_iter);

	/* find entry array */
	if (_t != last && _t->type == _TRACE_BIN_UNTOUCHED) {
		_t = first;
		trace_size = dbg->trace_iter;
	} else {
		trace_size = KDBUS_TRACE_MAX;
	}
	
	for (i = 0; i < trace_size; i++) {
		show_item(_t, s);

		if ((unsigned long)_t < (unsigned long)(last)) 
			_t++;
		else
			_t = first;
	}

	up_read(&dbg->rwlock);
	return;
}

static void kdbus_trace_free(struct kdbus_debug *dbg)
{
	int i;
	for (i = 0; i < KDBUS_TRACE_MAX; i++)
		if (trace_bin(dbg->trace, i).data)
			kfree(trace_bin(dbg->trace, i).data);
	kfree(dbg->trace);
}

static void kdbus_trace_init(struct kdbus_debug *dbg)
{
	dbg->trace = kcalloc(KDBUS_TRACE_MAX,
				 sizeof(struct kdbus_trace), GFP_KERNEL);
}
#else
#define kdbus_trace_init(d)
#define kdbus_trace_free(d)
#define __kdbus_trace_show(c,s)
#endif

/*
 * kdbus statistics
 */
static struct kmem_cache *stat_cachep;
struct {
		struct hlist_node h_ent;	
		struct plist_node p_ent;
		int pgid;
		char comm[TASK_COMM_LEN];
} typedef ConnNumStat_T;

void kdbus_stat_conn_num(struct kdbus_bus *bus, struct seq_file *s)
{
	ConnNumStat_T *cns, *r;
	DECLARE_HASHTABLE(pgid_hlist, 7);	/* 128 buckets */
	PLIST_HEAD(conn_num_plist);
	struct kdbus_conn *c;
	int i, conn_pgid;

	if (!stat_cachep)
		return;

	hash_init(pgid_hlist);

	/* Statistics number of connections according to PGID */
	down_read(&bus->conn_rwlock);
	hash_for_each(bus->conn_hash, i, c, hentry) {
		if (!kdbus_conn_active(c))
			continue;

		conn_pgid = kdbus_metadata_tgid(c); 
		cns = NULL;

		hash_for_each_possible(pgid_hlist, r, h_ent, conn_pgid)
			if (r->pgid == conn_pgid) {
				cns = r;
				break;
			}

		if (cns) {
			cns->p_ent.prio += 1;
			plist_del(&cns->p_ent, &conn_num_plist);
			plist_add(&cns->p_ent, &conn_num_plist);
		} else {
			cns = kmem_cache_alloc(stat_cachep, GFP_KERNEL);
			cns->pgid = conn_pgid;
			strncpy(cns->comm, kdbus_metadata_pid_comm(c), TASK_COMM_LEN); 
			hash_add(pgid_hlist, &cns->h_ent, conn_pgid);
			plist_node_init(&cns->p_ent, 1);
			plist_add(&cns->p_ent, &conn_num_plist);
		}
		
	}		
	up_read(&bus->conn_rwlock);

	i = 0;

	/* Print result and free resource */
	kdbus_debug_print(s, "-------------------------------------------+\n");
	kdbus_debug_print(s, " Bus : %-35s |\n", bus->node.name);
	kdbus_debug_print(s, "-------------------------------------------+\n");
	kdbus_debug_print(s, " idx |       PGcomm   |  Pid | Connections |\n");
	kdbus_debug_print(s, "-------------------------------------------+\n");
	list_for_each_entry_safe_reverse(cns, r,
			&conn_num_plist.node_list, p_ent.node_list) {
		kdbus_debug_print(s, " %4d %16s   %4d           %3d\n",
				i++, cns->comm, cns->pgid, cns->p_ent.prio);
		kmem_cache_free(stat_cachep, cns);
	}

	return;
} 

void kdbus_debug_stat_show(struct kdbus_bus *bus, kdbus_stat_t type)
{
	switch (type) {
	case KDBUS_STAT_CONN_NUM:
		kdbus_stat_conn_num(bus, NULL);
		break;
	default:
		pr_err("%s: Inlvalid type : %d\n", __func__, type);
		break;
	}
}

static void kdbus_debug_conn_info(struct kdbus_conn *conn, struct seq_file *s)
{
	struct kdbus_name_owner *owner;
	int idx = 0;
	char *print_fmt[] = {
		/* Debugfs format */
		"-----------------------------------"
		"-----------------------------------\n"
		"#  Connection   : %lld (Bus: %s)\n"
		"#  Thread       : %s / %d\n#  Thread Group : %s / %d\n"
		"#  Name Lists:\n",
		/* Debug log format */
		"| Conn:%lld Bus:%s T:%s/%d TG:%s/%d\n",
	};

	kdbus_debug_print(s, s ? print_fmt[0] : print_fmt[1],
		conn->id, conn->ep->bus->node.name,
		kdbus_metadata_tid_comm(conn), pid_vnr(conn->pid),
		kdbus_metadata_pid_comm(conn), kdbus_metadata_tgid(conn));

	if (s == NULL)
		return;

	list_for_each_entry(owner, &conn->names_list, conn_entry)
		kdbus_debug_print(s, "#   [%2d] %-40s\n", idx++, owner->name->name);

	kdbus_debug_print(s,
		"----------------------------------------------------------------------\n");
}

/*
 * debugfs connection file interfaces
 */
static int _kdbus_debug_conn_show(struct seq_file *s, void *data)
{
	struct kdbus_conn *conn;

	if (s == NULL)
		conn = (struct kdbus_conn *)data;
	else
		conn = (struct kdbus_conn *)s->private;

	if (!kdbus_conn_active(conn)) {
		kdbus_debug_print(s, "ERR: Conn(%lld) isn't active\n", conn->id);
		return 0;
	}

	kdbus_debug_conn_info(conn, s);

	__kdbus_trace_show(conn, s);

	return 0;
}

void kdbus_debug_conn_show(struct kdbus_conn *conn)
{
	_kdbus_debug_conn_show(NULL, conn);
}

static int kdbus_debug_conn_open(struct inode *inode, struct file *file)
{
	return single_open(file, _kdbus_debug_conn_show, inode->i_private);
}

struct file_operations kdbus_debug_conn_fops = {
	.open = kdbus_debug_conn_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release, 
};

/*
 * debugfs ring buffer file interfaces
 */
int kdbus_debug_log(const char *fmt, ...)
{
	va_list ap;
	struct ring_buffer_event *event;
	char local_buffer[256];
	int len = 0;

	if (!kdbus_debug_rb)
		return 0;

	va_start(ap, fmt);
	len += vscnprintf(local_buffer, sizeof(local_buffer), fmt, ap);
	event = ring_buffer_lock_reserve(kdbus_debug_rb, len + 1);
	if (!event) {
		len = 0;
		goto out;
	}
	memcpy(ring_buffer_event_data(event), local_buffer, len + 1);
	ring_buffer_unlock_commit(kdbus_debug_rb, event);
out:
	va_end(ap);
	return len;
}

static void *_rb_peek_next(struct ring_buffer_iter **rb_iter)
{
	struct ring_buffer_event *event;
	int cpu, next = -1;
	u64 ts, next_ts = ~0ULL;

	for_each_possible_cpu(cpu)	{
		event = ring_buffer_iter_peek(rb_iter[cpu], &ts);
		if (event && ts < next_ts) {
			next = cpu;
			next_ts = ts;
		}
	}

	return next != -1 ? rb_iter[next] : NULL;
}

static void *_rb_seq_start(struct seq_file *m, loff_t *pos)
{
	struct ring_buffer_iter **rb_iter = m->private;
	return _rb_peek_next(rb_iter);
}

static void *_rb_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ring_buffer_iter **rb_iter = m->private;
	return _rb_peek_next(rb_iter);
}

static void _rb_seq_stop(struct seq_file *m, void *p)
{
	/* No operation */
}

static int _rb_seq_show(struct seq_file *s, void *data)
{
	struct ring_buffer_iter *iter = data;
	struct ring_buffer_event *event;
	const char *str;
	u64 ts;
	unsigned long msec;

	event = ring_buffer_read(iter, &ts);
	if (!event)
		return 0;

	str = ring_buffer_event_data(event);

	msec = do_div(ts, NSEC_PER_SEC);
	msec /= NSEC_PER_MSEC;

	seq_printf(s, "[ %5llu.%03lu] %s", ts, msec, str);

	return 0;
}

static const struct seq_operations kdbus_debug_rb_seq_ops = {
	.start		= _rb_seq_start,
	.next		= _rb_seq_next,
	.stop		= _rb_seq_stop,
	.show		= _rb_seq_show,
};

static int kdbus_debug_rb_open(struct inode *inode, struct file *file)
{
	struct ring_buffer_iter **rb_iter;
	int cpu;

	rb_iter = __seq_open_private(file, &kdbus_debug_rb_seq_ops,
				sizeof(*rb_iter) * num_possible_cpus());
	if (!rb_iter)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		rb_iter[cpu] = ring_buffer_read_prepare(kdbus_debug_rb, cpu);
	}
	ring_buffer_read_prepare_sync();
	for_each_possible_cpu(cpu)
		ring_buffer_read_start(rb_iter[cpu]);

	return 0;
}

static int kdbus_debug_rb_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct ring_buffer_iter **rb_iter = m->private;
	int cpu;

	for_each_possible_cpu(cpu)
		ring_buffer_read_finish(rb_iter[cpu]);
	seq_release_private(inode, file);

	return 0;
}

struct file_operations kdbus_debug_rb_fops = {
	.open = kdbus_debug_rb_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = kdbus_debug_rb_release,
};

static struct dentry *kdbus_debug_dir;
static struct dentry *kdbus_debug_logger;

void kdbus_debug_bus_init(struct kdbus_bus *bus)
{
	bus->debug_dentry = debugfs_create_dir(bus->node.name, kdbus_debug_dir);
}

void kdbus_debug_bus_end(struct kdbus_bus *bus)
{
	debugfs_remove_recursive(bus->debug_dentry);
}

void kdbus_debug_conn_init(struct kdbus_conn *conn)
{
	struct kdbus_debug *dbg;
	char filename[16];

	conn->dbg = dbg = kzalloc(sizeof(struct kdbus_debug), GFP_KERNEL);
	kdbus_trace_init(dbg);
	snprintf(filename, sizeof(filename), "%lld", conn->id);
	dbg->debug_dentry = debugfs_create_file(filename, S_IRUGO | S_IWUGO,
							conn->ep->bus->debug_dentry,
							conn, &kdbus_debug_conn_fops);
	init_rwsem(&dbg->rwlock);
}

void kdbus_debug_conn_end(struct kdbus_conn *conn)
{
	struct kdbus_debug *dbg = conn->dbg;
	struct kdbus_bus *bus = conn->ep->bus;

	if (!dbg)
		return;

	conn->dbg = NULL;

	down_write(&dbg->rwlock);
	kdbus_trace_free(dbg);
	if (bus && kdbus_node_is_active(&bus->node))
		debugfs_remove(dbg->debug_dentry);
	up_write(&dbg->rwlock);
	kfree(dbg);
}

int kdbus_debug_init(void)
{
	int rb_size = PAGE_SIZE * 16;
	int rb_flags = RB_FL_OVERWRITE;
	kdbus_debug_dir = debugfs_create_dir("kdbus", NULL);
	stat_cachep = kmem_cache_create("kdbus_debug_stat",
				sizeof(ConnNumStat_T), 0, 0, NULL);
	kdbus_debug_rb = ring_buffer_alloc(rb_size, rb_flags);
	if (kdbus_debug_rb) {
		ring_buffer_set_clock(kdbus_debug_rb, local_clock);
		kdbus_debug_logger = debugfs_create_file("logger", S_IRUGO,
										kdbus_debug_dir, NULL,
										&kdbus_debug_rb_fops);
	}
	return 0;
}
late_initcall(kdbus_debug_init);
