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
#include <linux/fs.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>
#include <linux/slab.h>
#include "demux_ns.h"  /* demux specific header goes here */

/* function prototypes */
extern  bool  ns_cb(int demux_id, int plp_nr,
		gp_type_e type, const unsigned char *buff, int len);

struct ether_header {
	unsigned char    ether_dhost[6];
	unsigned char    ether_shost[6];
	unsigned short   ether_type;
};

struct kthread_t {
	struct task_struct *thread;
	struct socket *sock_send;
	struct sockaddr_in addr_send;
	int running;
};

struct kthread_t *kthread;

#define MODULE_NAME "ns_emulator"
#define TS_SIZE 4
#define SHEADER_SIZE  (TS_SIZE + sizeof(struct iphdr) + sizeof(struct udphdr))
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

#define ETH_P_IP        0x0800 /* Internet Protocol packet */

#define SOURCE_IP	"0.0.0.0"
#define DEST_IP		"0.0.0.0"
#define SRC_PORT 60001
#define DEST_PORT 60002

/*#define CB_SEND_2_PACKETS*/

#define  FILE_READ_BUFF_SIZE 1200 /* Modify as per max input packet size */
#define  HEADER_PAYLOAD_SIZE  (FILE_READ_BUFF_SIZE + SHEADER_SIZE)

static char *input_file = "/opt/payload";
unsigned int current_stamp;

module_param(input_file, charp, 0000);
MODULE_PARM_DESC(input_file, "A character string");

int get_file_size(unsigned char *filename)
{
	struct path p;
	struct kstat ks;

	kern_path(filename, 0, &p);
	vfs_getattr(&p, &ks);
	return ks.size;
}

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

unsigned char *add_ip_udp_header(unsigned char *buffer, int size)
{
	unsigned char *datagram;
	/* IP Header */
	struct iphdr *iph;
	/* UDP Header */
	struct udphdr *udph;

	datagram = kmalloc(size + SHEADER_SIZE - TS_SIZE, GFP_KERNEL);
	if (!datagram) {
		pr_err("Failed to kmalloc\n");
		return NULL;
	}

	iph   = (struct iphdr *) (datagram);
	udph  = (struct udphdr *) (datagram + sizeof(struct iphdr));

	/* Add IP Header values */
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 0;

	iph->tot_len = htons((size + SHEADER_SIZE - TS_SIZE));
	iph->id = htons(54321); /* Id of this packet */
	iph->frag_off = 0;
	iph->ttl = 255;
	iph->protocol = 17; /* IPPROTO_UDP */
	iph->check = 0;    /* Set to 0 before calculating checksum */
	iph->saddr = inet_addr(SOURCE_IP); /* Spoof the source ip address */
	iph->daddr = inet_addr(DEST_IP);

	iph->check = csum((unsigned short *) iph , sizeof(struct iphdr));
	/* UDP Header */
	udph->source = htons(SRC_PORT);
	udph->dest = htons(DEST_PORT);
	udph->len = htons(8 + size); /* header size */
	udph->check = 0; /* leave checksum 0 */

	/* copy the buffer data */
	memcpy(datagram + SHEADER_SIZE - TS_SIZE, buffer, size);
	return datagram;
}

int  read_from_file(struct file *in_fp, unsigned char *buf, unsigned int len)
{
	int size;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(get_ds());
	size = in_fp->f_op->read(in_fp, buf, len, &in_fp->f_pos);
	set_fs(fs);
	return size;
}

static void ns_emulator_start(void)
{
	int size;
#ifdef CB_SEND_2_PACKETS
	int bufsize = HEADER_PAYLOAD_SIZE * 2;
#else
	int bufsize = HEADER_PAYLOAD_SIZE;
#endif
	unsigned char buf[bufsize];
	unsigned char *data_packet;
	struct file *in_fp = NULL;
	int filesize = 0;
	unsigned char *delim = "start_of_file";
	unsigned int delay;
	unsigned int current_delay = 0;
	struct iphdr *pihdr;
	unsigned int plength = 0;

	/* kernel thread initialization */
	kthread->running = 1;
	current->flags |= PF_NOFREEZE;

	/* take care with signals, after daemonize() they are disabled */
	allow_signal(SIGKILL);
	current_stamp = 0;
#if 0
while (1) {
#endif

	/* send string start_of_file to indicate client that start of
	 * file content
	 */
	size = strlen(delim);
	data_packet = add_ip_udp_header(delim, size);
	if (!data_packet)
		goto out;

	size += SHEADER_SIZE - TS_SIZE;
	ns_cb(0, 0, 0, data_packet, size); /* call back to netstreamer */
	kfree(data_packet);

	in_fp = filp_open(input_file, O_RDONLY, 0644);
	if (!in_fp) {
		pr_err("Error opening file %s\n", input_file);
		goto out;
	}

	filesize = get_file_size(input_file);
	pr_info("file size: %s\n", input_file);
	in_fp->f_pos = 0;
	memset(buf, 0, bufsize);

	while (filesize > SHEADER_SIZE) {
		/* read timestamp and packet header  from file */
		size = read_from_file(in_fp, buf, SHEADER_SIZE);
		delay = 0;
		delay  |= (buf[0]<<24);
		delay  |= (buf[1]<<16);
		delay  |= (buf[2]<<8);
		delay  |= (buf[3]);

		if ((current_stamp == 0) && delay) {
			/* ns_cb will be called first time, save timestamp */
			current_stamp = delay;
			current_delay = 0;
		} else {
			if (delay < current_stamp) {
				/* TBD:Current timestamp < previous timestamp */
				pr_err("current ts < previous ts\n");
				break;
			}
			current_delay = delay - current_stamp;
			current_stamp = delay;
		}
		pihdr = (struct iphdr *) (buf + TS_SIZE);
		plength = ntohs(pihdr->tot_len)-(SHEADER_SIZE - TS_SIZE);
		if (plength > (HEADER_PAYLOAD_SIZE - TS_SIZE))
			break;
		/* read payload before sending to netstremer callback */
		size = read_from_file(in_fp, buf + SHEADER_SIZE, plength);
		if (size <= 0)
			break;
		size += SHEADER_SIZE;
#ifdef CB_SEND_2_PACKETS
		/* for test purpose: read  next packet to send 2
		 * packets in buff to ns_cb callback
		 */
		if (filesize > (size + SHEADER_SIZE)) {
			int size2;
			in_fp->f_pos += TS_SIZE; /* skip TS */
			size2 = read_from_file(in_fp, buf + size,
					SHEADER_SIZE - TS_SIZE);
			pihdr = (struct iphdr *) (buf + size);
			plength = ntohs(pihdr->tot_len)-(SHEADER_SIZE-TS_SIZE);
			if (plength > (HEADER_PAYLOAD_SIZE - TS_SIZE))
				break;
			size2 = read_from_file(in_fp,
				buf + size + SHEADER_SIZE - TS_SIZE, plength);
			if (size2 <= 0)
				break;
			size2 += (SHEADER_SIZE - TS_SIZE);
			size  += size2;
		}
#endif
		msleep(current_delay); /* delay as per timestamp */
		ns_cb(0, 0, 0, buf + TS_SIZE, size - TS_SIZE);

		filesize -= size;
	}
	pr_info("current delay: %x, last Tstamp: %x\n",
						current_delay, current_stamp);
	filp_close(in_fp, NULL);
	/* send string end_of_file to indicate client that end of
	 * file content
	 */
	delim = "end_of_file";
	size = strlen(delim);
	data_packet = add_ip_udp_header(delim, size);
	if (!data_packet)
		goto out;

	size += SHEADER_SIZE - TS_SIZE;
	ns_cb(0, 0, 0, data_packet, size); /* call back to netstreamer */
	kfree(data_packet);
#if 0 /* while loop end (for testing/sending same file again and again */
}
#endif
out:
	kthread->thread = NULL;
	kthread->running = 0;
}

int __init ns_emulator_init(void)
{
	kthread = kmalloc(sizeof(struct kthread_t), GFP_KERNEL);
	if (!kthread) {
		pr_err("Failed to kmalloc\n");
		return -ENOMEM;
	}

	memset(kthread, 0, sizeof(struct kthread_t));

	/* start kernel thread */
	kthread->thread =
		kthread_run((void *)ns_emulator_start, NULL, MODULE_NAME);
	if (IS_ERR(kthread->thread)) {
		pr_info(MODULE_NAME ": unable to start kernel thread\n");
		kfree(kthread);
		kthread = NULL;
		return -ENOMEM;
	}

	return 0;
}

void __exit ns_emulator_exit(void)
{
	if (kthread->thread == NULL)
		pr_info(MODULE_NAME": no kernel thread to kill\n");
	else {
		force_sig(SIGKILL, kthread->thread);

		while (kthread->running == 1)
			msleep(10);
		pr_info(MODULE_NAME ": succesfully killed kernel thread!\n");
	}

	/* free allocated resources before exit */
	kfree(kthread);
	kthread = NULL;

	pr_info(MODULE_NAME": module unloaded\n");
}

/* init and cleanup functions */
module_init(ns_emulator_init);
module_exit(ns_emulator_exit);

/* module information */
MODULE_DESCRIPTION("Emulator:Calls Netstreamer callback as per pkt Timestamp");
MODULE_AUTHOR("SRI-Delhi/SystemArch");
MODULE_LICENSE("GPL");
