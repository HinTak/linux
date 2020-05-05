#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>

#include <linux/errno.h>
#include <linux/types.h>

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/namei.h>

#include <linux/delay.h>

#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>
#include <linux/slab.h>
#include <linux/if_packet.h>

#define MODULE_NAME "netstreamer"

#define MY_SRC_MAC0	0x00
#define MY_SRC_MAC1	0x00
#define MY_SRC_MAC2	0x00
#define MY_SRC_MAC3	0x00
#define MY_SRC_MAC4	0x00
#define MY_SRC_MAC5	0x00

#define MY_DEST_MAC0	0xFF
#define MY_DEST_MAC1	0xFF
#define MY_DEST_MAC2	0xFF
#define MY_DEST_MAC3	0xFF
#define MY_DEST_MAC4	0xFF
#define MY_DEST_MAC5	0xFF

#define ETH_P_IP        0x0800          /* Internet Protocol packet     */

#define SOURCE_IP	"0.0.0.0"
#define DEST_IP		"0.0.0.0"

#undef CHECKSUM_CHECK

struct kthread_ts {
	struct task_struct *thread;
	struct socket *sock_send;
	struct sockaddr_ll addr_send;
	int running;
};

DEFINE_SPINLOCK(kthread_lock);

struct kthread_ts *kthread_s;

struct ether_header {
	unsigned char    ether_dhost[6];
	unsigned char    ether_shost[6];
	unsigned short   ether_type;
};

struct msghdr msg;
struct iovec iov[2];
struct ether_header eth_header;

#define HEADER_SIZE  (sizeof(struct ether_header) + \
		sizeof(struct iphdr) + sizeof(struct udphdr))
#define UDP_START  (sizeof(struct ether_header) + sizeof(struct iphdr))
#define ETH_SIZE  sizeof(struct ether_header)
/* function prototypes */
int netstreamer_send(struct socket *sock, struct sockaddr_ll *addr,
		int iovlen, int len);

/* demux related code/logic implementation goes here
 * we use buff, len argument as of current version of code
 */
#include "demux_ns.h"

unsigned int inet_addr(char *ip)
{
	int a, b, c, d;
	char addr[4];

	sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
	addr[0] = a;
	addr[1] = b;
	addr[2] = c;
	addr[3] = d;

	return *(unsigned int *)addr;
}

unsigned short csum(unsigned short *ptr, int nbytes)
{
	register long sum;
	unsigned short oddbyte;
	register short answer;

	sum = 0;
	while (nbytes > 1) {
		sum += *ptr++;
		nbytes -= 2;
	}
	if (nbytes == 1) {
		oddbyte = 0;
		*((u_char *)&oddbyte) = *(u_char *)ptr;
		sum += oddbyte;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum = sum + (sum >> 16);
	answer = (short)~sum;

	return answer;
}

void modify_packet_header(const unsigned char *src)
{
	/*IP Header*/
	struct iphdr *iphsrc = (struct iphdr *)src;
	/*UDP Header*/
	struct udphdr *udpsrc = (struct udphdr *)(src + sizeof(struct iphdr));

#if 1
	/* TODO: For Debug Purpose:
	 * Modify Source and destination IP address to 0.0.0.0 , as packets
	 * are not received at client app without this change
	 */
	iphsrc->saddr = inet_addr(SOURCE_IP);   /*Spoof the source ip address */
	iphsrc->daddr = inet_addr(DEST_IP);
	iphsrc->check = 0;      /*Set to 0 before calculating checksum */
	iphsrc->check = (csum((unsigned short *)iphsrc, sizeof(struct iphdr)));
#endif

#if 1 /*for test purpose: change udp checksum to 0*/
	udpsrc->check = 0;
#endif
}

/* find next packet: search for next packet based on Version = 4, IHL = 5 and
 * Protocol = 0x11 (UDP)
 */
bool find_next_packet(const unsigned char *buffer, int size, int *index)
{
	bool ret = false;

	if (size < (HEADER_SIZE - ETH_SIZE))
		return ret;
	*index = 0;
	/*search for 0x45 only  upto start of last 28 bytes*/
	size = size - (HEADER_SIZE - ETH_SIZE) + 1;
	for (*index = 0; *index < size; (*index)++) {
		if ((buffer[*index] == 0x45) &&
				(buffer[(*index) + 9] == 0x11))
			break;
	}
	if (*index < size)
		ret = true;

	return ret;
}

#ifdef CHECKSUM_CHECK
char  check_iphdr[20];
struct iphdr *thdr;
#endif
bool  ns_cb(int demux_id, int plp_nr, gp_type_e type,
		const unsigned char *buff, int len)
{
	ssize_t bytes_sent = -1;
	struct iphdr *pihdr;
	unsigned int cindex = 0, size;
	int start_index;
	bool ret = true;
	unsigned char *buffer;

	/* buff can have multiple IP packets so
	   need to get individual packet length from IP header
	 */
	while (len > 0) {
		buffer = (unsigned char *)(buff + cindex);
		pihdr = (struct iphdr *)(buffer);
		size =  ntohs(pihdr->tot_len);
		if ((size < (HEADER_SIZE - ETH_SIZE)) || (size > 65527)) {
			/* Search start of next IP Packet in remaining buff */
			cindex++;
			if (find_next_packet(buffer, --len,
						&start_index) == false)
				return false;/*No valid IP packet start found*/
			cindex += start_index;
			len -= start_index;
			continue;
		}
		iov[0].iov_base = &eth_header;
		iov[0].iov_len = sizeof(struct ethhdr);

		iov[1].iov_base = buffer;
		iov[1].iov_len = size;
#ifdef CHECKSUM_CHECK
		memcpy(check_iphdr, pihdr, sizeof(struct iphdr));
		thdr = (struct iphdr *)check_iphdr;
		thdr->check = 0;

		if (pihdr->check && (pihdr->check !=
		csum((unsigned short *)check_iphdr, sizeof(struct iphdr)))) {
			/*Integrity check of IP Header fail: Search start of
			 * next IP Packet in  remaining buff
			 */
			cindex++;
			if (find_next_packet(buffer, --len,
						&start_index) == false)
				return false; /*No valid IP packet found*/
			cindex += start_index;
			len -= start_index;
			continue;
		}
#endif
		/*Modify IP, UDP Header if required: no buffer copy */
		modify_packet_header(buffer);

		bytes_sent = netstreamer_send(kthread_s->sock_send,
				&kthread_s->addr_send, 2,
				size + sizeof(struct ethhdr));
		if (bytes_sent <= 0)
			return false;/*return  false to break out of the loop*/
		cindex += size;
		len -= size;
	}
	return ret;
}
EXPORT_SYMBOL(ns_cb);

static void netstreamer_start(void)
{
	int err;

	/* kernel thread initialization */
	kthread_s->running = 1;
	current->flags |= PF_NOFREEZE;

	/* take care with signals, after daemonize() they are disabled */
	allow_signal(SIGKILL);

	/* create a socket */
	err = sock_create(AF_PACKET, SOCK_RAW,
			IPPROTO_RAW, &kthread_s->sock_send);
	if (err < 0) {
		pr_err(MODULE_NAME
		": Could not create a datagram socket, error = %d\n", -ENXIO);
		kthread_s->thread = NULL;
		kthread_s->running = 0;
	}

	memset(&kthread_s->addr_send, 0, sizeof(struct sockaddr_ll));
	/* Index of the network device */
	kthread_s->addr_send.sll_ifindex = 1; /* if_idx.ifr_ifindex; */
	/* Address length*/
	kthread_s->addr_send.sll_halen = ETH_ALEN;
	/* Destination MAC */
	kthread_s->addr_send.sll_addr[0] = MY_DEST_MAC0;
	kthread_s->addr_send.sll_addr[1] = MY_DEST_MAC1;
	kthread_s->addr_send.sll_addr[2] = MY_DEST_MAC2;
	kthread_s->addr_send.sll_addr[3] = MY_DEST_MAC3;
	kthread_s->addr_send.sll_addr[4] = MY_DEST_MAC4;
	kthread_s->addr_send.sll_addr[5] = MY_DEST_MAC5;

	/*initialize  default values for msghdr */
	msg.msg_flags = 0;
	msg.msg_namelen  = sizeof(struct sockaddr_ll);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_control = NULL;

	/*create default ethernet header*/
	/*Add Ethernet header values*/
	eth_header.ether_shost[0] =  MY_SRC_MAC0;
	eth_header.ether_shost[1] =  MY_SRC_MAC1;
	eth_header.ether_shost[2] =  MY_SRC_MAC2;
	eth_header.ether_shost[3] =  MY_SRC_MAC3;
	eth_header.ether_shost[4] =  MY_SRC_MAC4;
	eth_header.ether_shost[5] =  MY_SRC_MAC5;
	eth_header.ether_dhost[0] =  MY_DEST_MAC0;
	eth_header.ether_dhost[1] =  MY_DEST_MAC1;
	eth_header.ether_dhost[2] =  MY_DEST_MAC2;
	eth_header.ether_dhost[3] =  MY_DEST_MAC3;
	eth_header.ether_dhost[4] =  MY_DEST_MAC4;
	eth_header.ether_dhost[5] =  MY_DEST_MAC5;

	eth_header.ether_type = htons(ETH_P_IP);
}

int netstreamer_send(struct socket *sock, struct sockaddr_ll *addr,
		int iovlen, int len)
{
	mm_segment_t oldfs;
	int size = 0;

	if (!sock->sk)
		return 0;

	msg.msg_name = addr;
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	/*size = packet_snd(sock, &msg, len);*/
	size = sock_sendmsg(sock, &msg, len);
	set_fs(oldfs);
	return size;
}

int demuxid;

int __init netstreamer_init(void)
{
	int ret;

	kthread_s = kmalloc(sizeof(*kthread_s), GFP_KERNEL);
	if (!kthread_s) {
		pr_err("Failed to kmalloc\n");
		return -ENOMEM;
	}

	memset(kthread_s, 0, sizeof(struct kthread_ts));

	/* initialize netstreamer*/
	netstreamer_start();

	/*Register callback*/
	demuxid = 0;
	ret = demux_set_net_streamer_cb(demuxid, (net_streamer_cb)ns_cb);
	if (ret != 0)
		pr_err("Error in demux_set_net_streamer_cb : %d\n", ret);

	return 0;
}

void __exit netstreamer_exit(void)
{
	int ret;

	ret = demux_unset_net_streamer_cb(demuxid, (net_streamer_cb)ns_cb, 0);
	if (ret != 0)
		pr_err("Error in demux_unset_net_streamer_cb : %d\n", ret);

	/*relase socket*/
	sock_release(kthread_s->sock_send);

	/* free allocated resources before exit */
	kfree(kthread_s);
	kthread_s = NULL;

	pr_info(MODULE_NAME ": module unloaded\n");
}

/* init and cleanup functions */
module_init(netstreamer_init);
module_exit(netstreamer_exit);

/* module information */
MODULE_DESCRIPTION("NetStreamer/Server sending data to user space APP");
MODULE_AUTHOR("SRI-Delhi/SystemArch");
MODULE_LICENSE("GPL");
