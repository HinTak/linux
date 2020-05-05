#ifndef __PACKET_INTERNAL_H__
#define __PACKET_INTERNAL_H__

#ifdef CONFIG_PACKET_PORT_SUPPORT
#define PKT_HTABLE_SIZE_MIN	(CONFIG_BASE_SMALL ? 128 : 256)

#define pkt_portaddr_for_each_entry(__sk, node, list) \
        hlist_nulls_for_each_entry(__sk, node, list, __sk_common.skc_portaddr_node)

#define pkt_portaddr_for_each_entry_rcu(__sk, node, list) \
        hlist_nulls_for_each_entry_rcu(__sk, node, list, __sk_common.skc_portaddr_node)

#endif

struct packet_mclist {
	struct packet_mclist	*next;
	int			ifindex;
	int			count;
	unsigned short		type;
	unsigned short		alen;
	unsigned char		addr[MAX_ADDR_LEN];
};

/* kbdq - kernel block descriptor queue */
struct tpacket_kbdq_core {
	struct pgv	*pkbdq;
	unsigned int	feature_req_word;
	unsigned int	hdrlen;
	unsigned char	reset_pending_on_curr_blk;
	unsigned char   delete_blk_timer;
	unsigned short	kactive_blk_num;
	unsigned short	blk_sizeof_priv;

	/* last_kactive_blk_num:
	 * trick to see if user-space has caught up
	 * in order to avoid refreshing timer when every single pkt arrives.
	 */
	unsigned short	last_kactive_blk_num;

	char		*pkblk_start;
	char		*pkblk_end;
	int		kblk_size;
	unsigned int	knum_blocks;
	uint64_t	knxt_seq_num;
	char		*prev;
	char		*nxt_offset;
	struct sk_buff	*skb;

	atomic_t	blk_fill_in_prog;

	/* Default is set to 8ms */
#define DEFAULT_PRB_RETIRE_TOV	(8)

	unsigned short  retire_blk_tov;
	unsigned short  version;
	unsigned long	tov_in_jiffies;

	/* timer to retire an outstanding block */
	struct timer_list retire_blk_timer;
};

struct pgv {
	char *buffer;
};

struct packet_ring_buffer {
	struct pgv		*pg_vec;
	unsigned int		head;
	unsigned int		frames_per_block;
	unsigned int		frame_size;
	unsigned int		frame_max;

	unsigned int		pg_vec_order;
	unsigned int		pg_vec_pages;
	unsigned int		pg_vec_len;

	struct tpacket_kbdq_core	prb_bdqc;
	atomic_t		pending;
};

extern struct mutex fanout_mutex;
#define PACKET_FANOUT_MAX	256

struct packet_fanout {
#ifdef CONFIG_NET_NS
	struct net		*net;
#endif
	unsigned int		num_members;
	u16			id;
	u8			type;
	u8			defrag;
	atomic_t		rr_cur;
	struct list_head	list;
	struct sock		*arr[PACKET_FANOUT_MAX];
	spinlock_t		lock;
	atomic_t		sk_ref;
	struct packet_type	prot_hook ____cacheline_aligned_in_smp;
};
#ifdef CONFIG_PACKET_PORT_SUPPORT
struct pkthdr
{
	__be16 s_port;
	__be16 d_port;
	__be32 s_addr;
	__be32 d_addr;
};

static inline struct pkthdr *pkt_hdr(const struct sk_buff *skb)
{
        return (struct pkthdr *)skb_network_header(skb);
}

extern unsigned long *sysctl_local_reserved_ports;

static inline int packet_is_reserved_local_port(int port)
{
        return test_bit(port, sysctl_local_reserved_ports);
}

/**
 *      struct packet_hslot - Packet hash slot
 *
 *      @head:  head of list of sockets
 *      @count: number of sockets in 'head' list
 *      @lock:  spinlock protecting changes to head/count
 */
struct packet_hslot {
        struct hlist_nulls_head head;
        int                     count;
        spinlock_t              lock;
} __attribute__((aligned(2 * sizeof(long))));

/**
 *      struct packet_table - PACKET table
 *
 *      @hash:  hash table, sockets are hashed on (local port)
 *      @hash2: hash table, sockets are hashed on (local port, local address)
 *      @mask:  number of slots in hash tables, minus 1
 *      @log:   log2(number of slots in hash table)
 */
struct packet_table {
        struct packet_hslot        *hash;
        struct packet_hslot        *hash2;
        unsigned int            mask;
        unsigned int            log;
};

extern struct packet_table packet_table;

static inline int packet_hashfn(struct net *net, unsigned num, unsigned mask)
{
        return (num + net_hash_mix(net)) & mask;
}

static inline struct packet_hslot *packet_hashslot(struct packet_table *table,
                                             struct net *net, unsigned int num)
{
        return &table->hash[packet_hashfn(net, num, table->mask)];
}
/*
 * For secondary hash, net_hash_mix() is performed before calling
 * udp_hashslot2(), this explains difference with udp_hashslot()
 */
static inline struct packet_hslot *packet_hashslot2(struct packet_table *table,
                                              unsigned int hash)
{
        return &table->hash2[hash & table->mask];
}

#endif

struct packet_sock {
	/* struct sock has to be the first member of packet_sock */
	struct sock		sk;
#ifdef CONFIG_PACKET_PORT_SUPPORT
/* Port in Short Host Format */
#define pkt_num		        sk.__sk_common.skc_num
#define pkt_dport		sk.__sk_common.skc_dport
#define pkt_ifindex     	sk.__sk_common.skc_rcv_saddr
#define pkt_difindex		sk.__sk_common.skc_daddr
#define pkt_port_hash       	sk.__sk_common.skc_u16hashes[0]
#define pkt_portaddr_hash	sk.__sk_common.skc_u16hashes[1]
#define pkt_portaddr_node	sk.__sk_common.skc_portaddr_node
#endif
	struct packet_fanout	*fanout;
	struct tpacket_stats	stats;
	union  tpacket_stats_u	stats_u;
	struct packet_ring_buffer	rx_ring;
	struct packet_ring_buffer	tx_ring;
	int			copy_thresh;
	spinlock_t		bind_lock;
	struct mutex		pg_vec_lock;
	unsigned int		running:1,	/* prot_hook is attached*/
				auxdata:1,
				origdev:1,
				has_vnet_hdr:1;
	int			ifindex;	/* bound device		*/
	#ifdef CONFIG_PACKET_PORT_SUPPORT
	/* Port as passed from app (Network Format) */
        __be16          	s_port;  
        #endif
	__be16			num;
	struct packet_mclist	*mclist;
	atomic_t		mapped;
	enum tpacket_versions	tp_version;
	unsigned int		tp_hdrlen;
	unsigned int		tp_reserve;
	unsigned int		tp_loss:1;
	unsigned int		tp_tx_has_off:1;
	unsigned int		tp_tstamp;
	struct packet_type	prot_hook ____cacheline_aligned_in_smp;
};

static struct packet_sock *pkt_sk(struct sock *sk)
{
	return (struct packet_sock *)sk;
}

#ifdef CONFIG_PACKET_PORT_SUPPORT
void packet_lib_unhash(struct sock *sk)
{
        if (sk_hashed(sk)) {
                struct packet_table *pkttable = sk->sk_prot->h.pkt_table;
                struct packet_hslot *hslot, *hslot2;

                hslot  = packet_hashslot(pkttable, sock_net(sk),
                                      pkt_sk(sk)->pkt_port_hash);
                hslot2 = packet_hashslot2(pkttable, pkt_sk(sk)->pkt_portaddr_hash);

                spin_lock_bh(&hslot->lock);
                if (sk_nulls_del_node_init_rcu(sk)) {
                        hslot->count--;
                        pkt_sk(sk)->pkt_num = 0;
                        sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);

                        spin_lock(&hslot2->lock);
                        hlist_nulls_del_init_rcu(&pkt_sk(sk)->pkt_portaddr_node);
                        hslot2->count--;
                        spin_unlock(&hslot2->lock);
                }
                spin_unlock_bh(&hslot->lock);
        }
}
EXPORT_SYMBOL(packet_lib_unhash);

#endif
#endif
