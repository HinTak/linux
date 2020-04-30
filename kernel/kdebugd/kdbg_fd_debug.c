#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/fdtable.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <kdebugd/kdebugd.h>
#include "agent/agent_packet.h"
#include "agent/tvis_agent_cmds.h"

#define FD_PATH_SIZE 128
#define FD_DETAIL_BUF_SIZE (140 * 1024)
#define DETAIL_PKT_CNT 50

static void kdbg_print_fd_info(void);
/* debug status: file fd debug status (by deault disabled) */
static atomic_t g_fd_debug_status = ATOMIC_INIT(0);

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
static void send_fd_detail_buffer(uint32_t packet_cnt_detail);
static void send_fd_count_buffer(uint32_t packet_cnt, uint32_t timestamp);
static void kdbg_get_open_fd_count(struct task_struct *tsk, uint32_t *packet_count,
		uint32_t *bytes, uint32_t buf_size);

#define fd_info_write(data, len) \
		agent_write(KDBG_CMD_CM_FD_INFO_CONT, data, len)
#define fd_info_detail_write(data, len) \
		agent_write(KDBG_CMD_CM_FD_DETAIL_CONT, data, len)

static char *fd_buffer;
static char *fd_detail_buffer;

static void send_fd_detail_buffer(uint32_t packet_cnt_detail)
{
	uint32_t bytes_written = 0;
	char *fd_detail_buf = fd_detail_buffer;

	if (packet_cnt_detail == 0)
		return;

	agent_write(KDBG_CMD_CM_FD_DETAIL_START, NULL, 0);

	while (packet_cnt_detail != 0) {
		if (packet_cnt_detail >= DETAIL_PKT_CNT) {
			packet_cnt_detail -= DETAIL_PKT_CNT;
			bytes_written = DETAIL_PKT_CNT * sizeof(struct fd_info);
		} else {
			bytes_written = packet_cnt_detail * sizeof(struct fd_info);
			packet_cnt_detail = 0;
		}

		fd_info_detail_write(fd_detail_buf, bytes_written);
		fd_detail_buf += bytes_written;
	}
	agent_write(KDBG_CMD_CM_FD_DETAIL_END, NULL, 0);
}

static void send_fd_count_buffer(uint32_t packet_cnt, uint32_t timestamp)
{
	uint32_t bytes_written = 0;

	if (packet_cnt == 0)
		return;

	agent_write(KDBG_CMD_CM_FD_INFO_START, &timestamp, sizeof(timestamp));
	bytes_written = packet_cnt * sizeof(struct fd_count);
	fd_info_write(fd_buffer, bytes_written);
	agent_write(KDBG_CMD_CM_FD_INFO_END, NULL, 0);
}

void kdbg_get_fd_info(void)
{
	struct task_struct *p = NULL;
	uint32_t packet_cnt = 0;
	uint32_t byte_written = 0;
	uint32_t buf_size = 0;
	uint32_t timestamp;

	buf_size = (uint32_t)nr_processes() * sizeof(struct fd_count);
	fd_buffer = vmalloc(buf_size);
	if (fd_buffer == NULL) {
		printk("Err:Not enough memory for fd count buffer\n");
		return;
	}

	timestamp = (uint32_t)kdbg_get_uptime();

	rcu_read_lock();

	for_each_process(p) {
		kdbg_get_open_fd_count(p, &packet_cnt, &byte_written, buf_size);
	}

	rcu_read_unlock();

	send_fd_count_buffer(packet_cnt, timestamp);

	vfree(fd_buffer);
}


static void kdbg_get_open_fd_count(struct task_struct *tsk,
		uint32_t *packet_count, uint32_t *bytes, uint32_t buf_size)
{
	uint32_t i = 0;
	struct file *file;
	struct fdtable *fdt;
	struct files_struct *tsk_files_pt = NULL;
	struct fd_count fd_pack;

	fd_pack.fd_cnt = 0;

	if (!tsk || !buf_size) {
		printk("tsk is null\n");
		goto fd_count_exit;
	}

	get_task_struct(tsk);

	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		printk("Err:Task files pointer is NULL\n");
		goto fd_count_exit;
	}

	fdt = files_fdtable(tsk_files_pt);
	if (fdt == NULL) {
		printk("Err:File table pointer is NULL\n");
		goto fd_count_exit;
	}

	while (i < fdt->max_fds) {
		if (fdt->fd[i] == NULL) {
			i++;
			continue;
		}

		file = fcheck_files(tsk_files_pt, i);
		if (!file) {
			printk("Err:No file for given fd %d\n", i);
			i++;
			continue;
		}
		/*Increment open fd count*/
		fd_pack.fd_cnt++;

		i++;
	}

	fd_pack.tgid = tsk->tgid;

	if (*bytes <= (buf_size - sizeof(struct fd_count))) {
		if (fd_pack.fd_cnt > 0) {
			memcpy(fd_buffer + *bytes, &fd_pack, sizeof(struct fd_count));
			*packet_count = *packet_count + 1;
			*bytes += sizeof(struct fd_count);
		}
	} else
		printk("Err:No space in fd count buffer\n");

fd_count_exit:
	if (tsk_files_pt)
		put_files_struct(tsk_files_pt);
	if (tsk)
		put_task_struct(tsk);
}

void kdebugd_send_fd_detail(int32_t tgid)
{
	uint32_t i = 0, bytes = 0;
	int32_t is_socket = 0;
	char *tmp = NULL;
	char *pathname = NULL;
	uint32_t packet_cnt_detail = 0;
	struct file *file = NULL;
	struct path *path;
	struct fdtable *fdt;
	struct inode *inode;
	struct task_struct *tsk = NULL;
	struct files_struct *tsk_files_pt = NULL;
	struct fd_info *open_fd_detail = NULL;

	if (tgid < 0) {
		printk("invalid tgid\n");
		goto exit_send1;
	}

	fd_detail_buffer = NULL;

	rcu_read_lock();
	tsk = find_task_by_pid_ns(tgid, &init_pid_ns);

	if (!tsk) {
		printk("tsk is null\n");
		goto exit_send;
	}

	get_task_struct(tsk);
	rcu_read_unlock();

	fd_detail_buffer = vmalloc(FD_DETAIL_BUF_SIZE);
	if (fd_detail_buffer == NULL) {
		printk("Err:Not enough memory for fd detail buffer\n");
		goto exit_send1;
	}

	tmp = kmalloc(FD_PATH_SIZE, GFP_KERNEL);
	if (!tmp) {
		printk("Err:Not enough memory\n");
		goto exit_send1;
	}

	tmp[0] = '\0';

	open_fd_detail = kmalloc(sizeof(struct fd_info), GFP_KERNEL);
	if (open_fd_detail == NULL) {
		printk("Err:Not sufficient memory\n");
		goto exit_send1;
	}

	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		printk("Err:Task files pointer is NULL\n");
		goto exit_send1;
	}

	rcu_read_lock();

	fdt = files_fdtable(tsk->files);
	if (fdt == NULL) {
		printk("Err:File table pointer is NULL\n");
		goto exit_send;
	}

	while (i < fdt->max_fds) {
		if (fdt->fd[i] == NULL) {
			i++;
			continue;
		}

		file = fcheck_files(tsk_files_pt, i);
		if (!file) {
			printk("Err:No file for given fd %d\n", i);
			i++;
			continue;
		}

		path = &file->f_path;
		path_get(path);

		pathname = d_path(path, tmp, FD_PATH_SIZE);
		path_put(path);

		if (IS_ERR(pathname)) {
			printk("Error in path name\n");
			i++;
			continue;
		}

		mutex_lock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
		inode = file->f_path.dentry->d_inode;
		if (inode != NULL) {
			is_socket = S_ISSOCK(inode->i_mode);
			mutex_unlock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
		} else {
			i++;
			mutex_unlock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
			continue;
		}

		if (is_socket) {
			open_fd_detail->fd = i;
			open_fd_detail->tgid = tsk->tgid;
			open_fd_detail->fd_type = T_SOCKET;
			memcpy(open_fd_detail->name, pathname, FD_PATH_SIZE);

			if (bytes <= (FD_DETAIL_BUF_SIZE - sizeof(struct fd_info))) {
				memcpy(fd_detail_buffer+bytes, open_fd_detail, sizeof(struct fd_info));
				bytes += sizeof(struct fd_info);
				packet_cnt_detail++;
			} else
				printk("Err:No space in fd detail buffer\n");
		} else {

			open_fd_detail->fd = i;
			open_fd_detail->tgid = tsk->tgid;
			open_fd_detail->fd_type = T_FILE;
			memcpy(open_fd_detail->name, pathname, FD_PATH_SIZE);

			if (bytes <= (FD_DETAIL_BUF_SIZE - sizeof(struct fd_info))) {
				memcpy(fd_detail_buffer+bytes, open_fd_detail, sizeof(struct fd_info));
				bytes += sizeof(struct fd_info);
				packet_cnt_detail++;
			} else
				printk("Err:No space in fd detail buffer\n");
		}
		i++;
	}

exit_send:
	rcu_read_unlock();
exit_send1:
	if (open_fd_detail != NULL)
		kfree(open_fd_detail);

	kfree(tmp);

	if (tsk_files_pt)
		put_files_struct(tsk_files_pt);
	if (tsk)
		put_task_struct(tsk);

	send_fd_detail_buffer(packet_cnt_detail);

	if (fd_detail_buffer)
		vfree(fd_detail_buffer);
}
#endif

int kdbg_fd_debug_status(void)
{
	return  atomic_read(&g_fd_debug_status);
}

int kdbg_fd_debug_handler(void)
{

	int operation = 0;

	while (1) {
		PRINT_KD("-----------------------------------\n");
		PRINT_KD("Current FD Debug Status: %s\n",
				atomic_read(&g_fd_debug_status) ?
				"ENABLE" : "DISABLE");
		PRINT_KD("-------------------------------------\n");
		PRINT_KD("1.  For Toggle Status\n");
		PRINT_KD("2.  Print open fd info\n");
		PRINT_KD("99. For Exit\n");
		PRINT_KD("--------------------------------------\n");
		PRINT_KD("Select Option==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);

		if (operation == 1)
			atomic_set(&g_fd_debug_status
					, !atomic_read(&g_fd_debug_status));
		else if (operation == 2)
			kdbg_print_fd_info();
		else if (operation == 99)
			break;
		else
			PRINT_KD("Invalid Option..\n");
	}

	return 1;
}

static void kdbg_print_fd_info(void)
{
	struct task_struct *tsk = NULL;
	int task_pid = 0;
	debugd_event_t core_event;
	uint32_t i = 0;
	struct fdtable *fdt;
	char *tmp;
	char *pathname;
	struct file *file;
	struct path *path;
	unsigned long cur_max_fd = 0;
	unsigned long max_fd = 0;
	int fd_count = 0;
	struct files_struct *tsk_files_pt = NULL;

	PRINT_KD("\n");
	PRINT_KD("Enter pid\n");
	PRINT_KD("==>");

	kdbg_set_tab_action(KDBG_TAB_SHOW_TASK);
	task_pid = debugd_get_event_as_numeric(&core_event, NULL);
	if (task_pid < 0)
		task_pid = kdbg_tab_process_event(&core_event);
	kdbg_set_tab_action(KDBG_TAB_NO_ACTION);

	PRINT_KD("\n");

	rcu_read_lock();

	tsk = find_task_by_pid_ns(task_pid, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (!tsk) {
		printk("Invalid task ID\n");
		return;
	}

	tmp = kmalloc(FD_PATH_SIZE, GFP_KERNEL);
	if (!tmp) {
		printk("Err:Not enough memory\n");
		put_task_struct(tsk);
		return;
	}

	tmp[0] = '\0';

	cur_max_fd = task_rlimit(tsk, RLIMIT_NOFILE);
	max_fd = task_rlimit_max(tsk, RLIMIT_NOFILE);

	printk("-----------------------------------------------\n");
	printk("Max fd Soft limit : %lu\n", cur_max_fd);
	printk("Max fd Hard limit : %lu\n", max_fd);

	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		printk("Err:Task files pointer is NULL\n");
		kfree(tmp);
		put_task_struct(tsk);
		return;
	}

	fdt = files_fdtable(tsk->files);
	if (fdt == NULL) {
		printk("Err:File table pointer is NULL\n");
		kfree(tmp);
		put_files_struct(tsk_files_pt);
		put_task_struct(tsk);
		return;
	}

	while (i < fdt->max_fds) {
		if (fdt->fd[i] == NULL) {
			i++;
			continue;
		}
		spin_lock(&tsk_files_pt->file_lock);
		file = fcheck_files(tsk_files_pt, i);
		if (!file) {
			spin_unlock(&tsk_files_pt->file_lock);
			printk("Err:No file for given fd %d\n", i);
			i++;
			continue;
		}

		path = &file->f_path;
		path_get(path);
		spin_unlock(&tsk_files_pt->file_lock);

		pathname = d_path(path, tmp, FD_PATH_SIZE);
		path_put(path);

		if (IS_ERR(pathname)) {
			printk("Error in path name\n");
			i++;
			continue;
		}

		if (fd_count == 0) {
			printk("-----------------------------------------------\n");
			printk(" FD |  Type   |        Path\n");
			printk("-----------------------------------------------\n");
		}

		printk("%3d", i);
		if (S_ISSOCK(file->f_path.dentry->d_inode->i_mode))
			printk("   %-7s", "Socket");
		else
			printk("   %-7s", "FILE");
		printk("       %-20s\n", pathname);
		fd_count++;
		i++;
	}
	printk("-----------------------------------------------\n");
	printk("Number of open fd : %d\n", fd_count);
	printk("-----------------------------------------------\n");

	kfree(tmp);
	put_files_struct(tsk_files_pt);
	put_task_struct(tsk);
}

