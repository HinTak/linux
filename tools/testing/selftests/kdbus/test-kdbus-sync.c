#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>

#include "kdbus-util.h"
#include "kdbus-enum.h"

static struct conn *conn_a, *conn_b;
static unsigned int cookie = 0xdeadbeef;

static int msg_get_pid_and_tid(const struct kdbus_msg *msg, pid_t *pid, pid_t *tid)
{
	const struct kdbus_item *item;

	assert(msg);
	assert(pid);
	assert(tid);

	item = msg->items;
	KDBUS_ITEM_FOREACH(item, msg, items) {
		if (item->size < KDBUS_ITEM_HEADER_SIZE) {
			fprintf(stderr, "Invalid data record\n");
			break;
		}
		if (item->type == KDBUS_ITEM_CREDS) {
			*pid = (pid_t)(item->creds.pid);
			*tid = (pid_t)(item->creds.tid);
			return 0;
		}
	}
	return -EIO;
}

static void *run_thread(void *data)
{
	struct pollfd fd;
	int ret;
	int offset;
	pid_t pid = 0;
	pid_t tid = 0;
	pid_t my_tid = syscall(SYS_gettid);

	fd.fd = conn_a->fd;
	fd.events = POLLIN | POLLPRI | POLLHUP;
	fd.revents = 0;

	ret = poll(&fd, 1, 3000);
	if (ret <= 0)
		return NULL;

	if (fd.revents & POLLIN) {
		printf("Thread received message, sending reply ...\n");

		offset = msg_recv(conn_a);

		if (offset < 0) {
			fprintf(stderr,
				"Could not receive message\n");
			exit(EXIT_FAILURE);
		}

		ret = msg_get_pid_and_tid(
			(struct kdbus_msg *)(conn_a->buf + offset), &pid, &tid);
		if (ret)
			exit(EXIT_FAILURE);
		if (pid != getpid()) {
			fprintf(stderr,
				"Both threads should share the same pid\n");
			exit(EXIT_FAILURE);
		}
		if (pid != tid) {
			fprintf(stderr,
				"Sending thread is main thread, "
				"should have pid == tid\n");
			exit(EXIT_FAILURE);
		}
		if (tid == my_tid) {
			fprintf(stderr,
				"Sending thread should have different tid\n");
			exit(EXIT_FAILURE);
		}

		offset = msg_send(conn_a, NULL, 0, 0, cookie, 0, conn_b->id);

		if (offset < 0) {
			fprintf(stderr,
				"Could not send message\n");
			exit(EXIT_FAILURE);
		}

		ret = msg_get_pid_and_tid(
			(struct kdbus_msg *)(conn_b->buf + offset), &pid, &tid);
		if (ret)
			exit(EXIT_FAILURE);
		if (pid != getpid()) {
			fprintf(stderr,
				"Both threads should share the same pid\n");
			exit(EXIT_FAILURE);
		}
		if (pid == tid) {
			fprintf(stderr,
				"Replying thread is not a main thread, "
				"should have pid != tid\n");
			exit(EXIT_FAILURE);
		}
		if (tid != my_tid) {
			fprintf(stderr,
				"Replying thread should send its own tid\n");
			exit(EXIT_FAILURE);
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct {
		struct kdbus_cmd_make head;

		/* bloom size item */
		struct {
			uint64_t size;
			uint64_t type;
			struct kdbus_bloom_parameter bloom;
		} bs;

		/* name item */
		uint64_t n_size;
		uint64_t n_type;
		char name[64];
	} bus_make;
	pthread_t thread;
	int fdc, ret;
	char *bus;

	printf("-- opening /dev/" KBUILD_MODNAME "/control\n");
	fdc = open("/dev/" KBUILD_MODNAME "/control", O_RDWR|O_CLOEXEC);
	if (fdc < 0) {
		fprintf(stderr, "--- error %d (%m)\n", fdc);
		return EXIT_FAILURE;
	}

	memset(&bus_make, 0, sizeof(bus_make));
	bus_make.bs.size = sizeof(bus_make.bs);
	bus_make.bs.type = KDBUS_ITEM_BLOOM_PARAMETER;
	bus_make.bs.bloom.size = 64;
	bus_make.bs.bloom.n_hash = 1;

	snprintf(bus_make.name, sizeof(bus_make.name), "%u-testbus", getuid());
	bus_make.n_type = KDBUS_ITEM_MAKE_NAME;
	bus_make.n_size = KDBUS_ITEM_HEADER_SIZE + strlen(bus_make.name) + 1;

	bus_make.head.size = sizeof(struct kdbus_cmd_make) +
			     sizeof(bus_make.bs) +
			     bus_make.n_size;

	printf("-- creating bus '%s'\n", bus_make.name);
	ret = ioctl(fdc, KDBUS_CMD_BUS_MAKE, &bus_make);
	if (ret) {
		fprintf(stderr, "--- error %d (%m)\n", ret);
		return EXIT_FAILURE;
	}

	if (asprintf(&bus, "/dev/" KBUILD_MODNAME "/%s/bus", bus_make.name) < 0)
		return EXIT_FAILURE;

	conn_a = kdbus_hello(bus, 0);
	conn_b = kdbus_hello(bus, 0);
	if (!conn_a || !conn_b)
		return EXIT_FAILURE;

	pthread_create(&thread, NULL, run_thread, NULL);

	msg_send(conn_b, NULL, cookie, KDBUS_MSG_FLAGS_EXPECT_REPLY | KDBUS_MSG_FLAGS_SYNC_REPLY,
		 5000000000ULL, 0, conn_a->id);

	printf("-- closing bus connections\n");
	close(conn_a->fd);
	close(conn_b->fd);
	free(conn_a);
	free(conn_b);

	printf("-- closing bus master\n");
	close(fdc);
	free(bus);

	return EXIT_SUCCESS;
}
