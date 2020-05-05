#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/fdtable.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <kdebugd/kdebugd.h>
#include "agent/agent_packet.h"
#include "agent/tvis_agent_cmds.h"
#include "agent/agent_core.h"
#include "kdbg-trace.h"

#ifdef CONFIG_KDEBUGD_FD_DEBUG
#define FD_PATH_SIZE 128
#define FD_DETAIL_BUF_SIZE (140 * 1024)
#define DETAIL_PKT_CNT 50

#define FD_HASH_SIZE 50
#define FD_MAX_BT_DEPTH 7
#define FD_MAX_BT_NUM 50

static void kdbg_fd_overflow_info(void);
static void kdbg_print_fd_info(void);
/* debug status: file fd debug status (by deault disabled) */
static atomic_t g_fd_debug_status = ATOMIC_INIT(0);


struct fd_pro_node *hash_array[FD_HASH_SIZE];
DEFINE_MUTEX(hash_lock);

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
static void send_fd_detail_buffer(uint32_t packet_cnt_detail);

struct fd_info {
	uint32_t fd;
	int32_t tgid;
	uint32_t tid;
	char name[128];
};

struct kdbg_proc_fd_info {
	uint32_t tgid;
	uint32_t fd_type[4];
};

struct kdbg_proc_fd_bt {
	uint32_t tgid;
	uint32_t type;
	uint32_t pc[7];
};

#define fd_info_write(data, len) \
		agent_write(KDBG_CMD_CM_FD_INFO_CONT, data, len)
#define fd_info_detail_write(data, len) \
		agent_write(KDBG_CMD_CM_FD_DETAIL_CONT, data, len)

static struct kdbg_pid_fd_bt fd_bt_pid = {1, -1};
void kdbg_set_fd_bt(struct kdbg_pid_fd_bt *fd_data)
{
	if (fd_bt_pid.event == fd_data->event && fd_bt_pid.pid == fd_data->pid) {
		PRINT_KD ("State change is not valid Prev(%d) New(%d)\n",
				fd_bt_pid.event, fd_data->event);
		return;
	}
	fd_bt_pid.event = fd_data->event;

	PRINT_KD("%s:%u BT Command received %d Process %d\n", __FILE__, __LINE__,
			fd_data->event, fd_data->pid);
	if (fd_data->event == CM_START)
		fd_bt_pid.pid = fd_data->pid;
	else
		fd_bt_pid.pid = -1;
}

int kdbg_get_bt_status(pid_t pid)
{
	if (fd_bt_pid.event == CM_START && fd_bt_pid.pid == pid)
		return 0;
	return -1;
}


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

static void send_fd_count_buffer(void *fd_buffer, uint32_t packet_cnt, uint32_t timestamp)
{
	uint32_t bytes_written = 0;

	if (packet_cnt == 0)
		return;

	agent_write(KDBG_CMD_CM_FD_INFO_START, &timestamp, sizeof(timestamp));
	bytes_written = packet_cnt * sizeof(struct kdbg_proc_fd_info);
	fd_info_write(fd_buffer, bytes_written);
	agent_write(KDBG_CMD_CM_FD_INFO_END, NULL, 0);
}


static int get_proc_fd_detail (struct task_struct *tsk,
		struct kdbg_proc_fd_info *pfd_info)
{
	struct fdtable *fdt = NULL;
	struct files_struct *tsk_files = NULL;
	int ret = -1;
	if (!tsk || !pfd_info) {
		PRINT_KD("Task doesnt not Exits\n");
		return ret;
	}
	get_task_struct(tsk);
	pfd_info->tgid = tsk->tgid;
	tsk_files = get_files_struct(tsk);
	if (tsk_files)
		fdt = files_fdtable(tsk_files);
	if (fdt) {
		/*FIXME: do any protection needed for fdt !!!*/
		pfd_info->fd_type[0] = fdt->file_fdn;
		pfd_info->fd_type[1] = fdt->socket_fdn;
		pfd_info->fd_type[2] = fdt->pipe_fdn;
		pfd_info->fd_type[3] = fdt->etc_fdn;
		ret = 0;
#ifdef KDBG_FD_DEBUG
	PRINT_KD("%s:%u data: tgid %d, F(%d) S(%d) P(%d) E(%d)\n", __FILE__, __LINE__,
			pfd_info->tgid,
			pfd_info->fd_type[0],
			pfd_info->fd_type[1],
			pfd_info->fd_type[2],
			pfd_info->fd_type[3]);
#endif
	}
	if (tsk_files)
		put_files_struct(tsk_files);
	put_task_struct(tsk);
	return ret;
}

void kdbg_get_fd_info(void)
{
	struct task_struct *p = NULL;
	uint32_t i = 0;
	uint32_t timestamp;
	uint32_t nr_proc = nr_processes();
	char *fd_buffer;

	/* TODO: Its not a good Idea to allocate everytime,
	 * should have allocated 1 time and when start message received.
	 * and deallocate when stop command recieved */
	fd_buffer = vmalloc(nr_proc * sizeof(struct kdbg_proc_fd_info));
	if (fd_buffer == NULL) {
		PRINT_KD("Err:Not enough memory for fd count buffer\n");
		return;
	}

	timestamp = (uint32_t)kdbg_get_uptime();

	rcu_read_lock();
	for_each_process(p) {
		/* Remove Kernel threads*/
		if (!p->mm)
			continue;
		if (i < nr_proc && !get_proc_fd_detail(p, ((struct kdbg_proc_fd_info *)fd_buffer) + i))
			i++;
	}
	rcu_read_unlock();

	send_fd_count_buffer((void *)fd_buffer, i, timestamp);

	vfree(fd_buffer);
}

void kdebugd_send_fd_detail(int32_t tgid)
{
	uint32_t i = 0, bytes = 0;
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
		PRINT_KD("invalid tgid\n");
		goto exit_send1;
	}

	fd_detail_buffer = NULL;

	rcu_read_lock();
	tsk = find_task_by_pid_ns(tgid, &init_pid_ns);

	if (!tsk) {
		PRINT_KD("tsk is null\n");
		goto exit_send;
	}

	get_task_struct(tsk);
	rcu_read_unlock();

	fd_detail_buffer = vmalloc(FD_DETAIL_BUF_SIZE);
	if (fd_detail_buffer == NULL) {
		PRINT_KD("Err:Not enough memory for fd detail buffer\n");
		goto exit_send1;
	}

	tmp = kmalloc(FD_PATH_SIZE, GFP_KERNEL);
	if (!tmp) {
		PRINT_KD("Err:Not enough memory\n");
		goto exit_send1;
	}

	tmp[0] = '\0';

	open_fd_detail = kmalloc(sizeof(struct fd_info), GFP_KERNEL);
	if (open_fd_detail == NULL) {
		PRINT_KD("Err:Not sufficient memory\n");
		goto exit_send1;
	}

	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		PRINT_KD("Err:Task files pointer is NULL\n");
		goto exit_send1;
	}

	rcu_read_lock();

	fdt = files_fdtable(tsk->files);
	if (fdt == NULL) {
		PRINT_KD("Err:File table pointer is NULL\n");
		goto exit_send;
	}

	while (i < fdt->max_fds) {
		if (fdt->fd[i] == NULL) {
			i++;
			continue;
		}

		file = fcheck_files(tsk_files_pt, i);
		if (!file) {
			PRINT_KD("Err:No file for given fd %d\n", i);
			i++;
			continue;
		}

		path = &file->f_path;
		path_get(path);

		pathname = d_path(path, tmp, FD_PATH_SIZE);
		path_put(path);

		if (IS_ERR(pathname)) {
			PRINT_KD("Error in path name\n");
			i++;
			continue;
		}

		mutex_lock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
		inode = file->f_path.dentry->d_inode;
		if (inode != NULL) {
			mutex_unlock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
		} else {
			i++;
			mutex_unlock(&file->f_path.dentry->d_parent->d_inode->i_mutex);
			continue;
		}

		open_fd_detail->fd = i;
		open_fd_detail->tgid = tsk->tgid;
		open_fd_detail->tid = tsk->pid;
		memcpy(open_fd_detail->name, pathname, FD_PATH_SIZE);
		if (bytes <= (FD_DETAIL_BUF_SIZE - sizeof(struct fd_info))) {
			memcpy(fd_detail_buffer+bytes, open_fd_detail, sizeof(struct fd_info));
			bytes += sizeof(struct fd_info);
			packet_cnt_detail++;
		} else
			PRINT_KD("Err:No space in fd detail buffer\n");
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
		PRINT_KD("3.  Dangerous process - fd overflow\n");
		PRINT_KD("99. For Exit\n");
		PRINT_KD("--------------------------------------\n");
		PRINT_KD("Select Option==> ");
		operation = debugd_get_event_as_numeric(NULL, NULL);

		if (operation == 1)
			atomic_set(&g_fd_debug_status
					, !atomic_read(&g_fd_debug_status));
		else if (operation == 2)
			kdbg_print_fd_info();
		else if (operation == 3)
			kdbg_fd_overflow_info();
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
		PRINT_KD("Invalid task ID\n");
		return;
	}

	tmp = kmalloc(FD_PATH_SIZE, GFP_KERNEL);
	if (!tmp) {
		PRINT_KD("Err:Not enough memory\n");
		put_task_struct(tsk);
		return;
	}

	tmp[0] = '\0';

	cur_max_fd = task_rlimit(tsk, RLIMIT_NOFILE);
	max_fd = task_rlimit_max(tsk, RLIMIT_NOFILE);

	PRINT_KD("-----------------------------------------------\n");
	PRINT_KD("Max fd Soft limit : %lu\n", cur_max_fd);
	PRINT_KD("Max fd Hard limit : %lu\n", max_fd);

	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		PRINT_KD("Err:Task files pointer is NULL\n");
		kfree(tmp);
		put_task_struct(tsk);
		return;
	}

	fdt = files_fdtable(tsk->files);
	if (fdt == NULL) {
		PRINT_KD("Err:File table pointer is NULL\n");
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
			PRINT_KD("Err:No file for given fd %d\n", i);
			i++;
			continue;
		}

		path = &file->f_path;
		path_get(path);
		spin_unlock(&tsk_files_pt->file_lock);

		pathname = d_path(path, tmp, FD_PATH_SIZE);
		path_put(path);

		if (IS_ERR(pathname)) {
			PRINT_KD("Error in path name\n");
			i++;
			continue;
		}

		if (fd_count == 0) {
			PRINT_KD("-----------------------------------------------\n");
			PRINT_KD(" FD |  Type   |        Path\n");
			PRINT_KD("-----------------------------------------------\n");
		}

		if (S_ISSOCK(file->f_path.dentry->d_inode->i_mode))
			PRINT_KD("%3d   %-7s       %-20s\n", i, "Socket", pathname);
		else
			PRINT_KD("%3d   %-7s       %-20s\n", i, "FILE", pathname);
		fd_count++;
		i++;
	}
	PRINT_KD("-----------------------------------------------\n");
	PRINT_KD("Number of open fd : %d\n", fd_count);
	PRINT_KD("-----------------------------------------------\n");
	PRINT_KD("  file type : %u\n", fdt->file_fdn);
	PRINT_KD("socket type : %u\n", fdt->socket_fdn);
	PRINT_KD("  pipe type : %u\n", fdt->pipe_fdn);
	PRINT_KD("        etc : %u\n", fdt->etc_fdn);
	PRINT_KD("-----------------------------------------------\n");

	kfree(tmp);
	put_files_struct(tsk_files_pt);
	put_task_struct(tsk);
}

struct bt_node {
	int fd;
	int fd_type;
	struct kdbg_bt_buffer *bt;

	struct list_head list;
};

struct fd_pro_node {
	pid_t pid;
	unsigned int input_num;
	struct bt_node *bt_queue;

	struct list_head list;
};

static struct bt_node *fd_bt_queue_init(unsigned int fd_type, unsigned int fd)
{
	struct bt_node *node;

	node = (struct bt_node *)kzalloc(sizeof(struct bt_node), GFP_KERNEL);

	if (!node)
		goto err1;

	node->fd = fd;
	node->fd_type = fd_type;
	node->bt = (struct kdbg_bt_buffer *)kzalloc(sizeof(struct kdbg_bt_buffer), GFP_KERNEL);

	if (!node->bt)
		goto err2;

	node->bt->max_entries = FD_MAX_BT_DEPTH;
	node->bt->symbol = (struct bt_frame *)kzalloc(
			sizeof(struct bt_frame)*node->bt->max_entries, GFP_KERNEL);

	if (!node->bt->symbol)
		goto err3;

	return node;

err3:
	kfree(node->bt);
err2:
	kfree(node);
err1:
	return NULL;
}

static void fd_get_bt(pid_t pid, struct bt_node *node)
{
	show_user_backtrace_pid(pid, 0, 1, node->bt);
}

static void fd_enqueue(struct fd_pro_node *p_node, struct bt_node *new_bt_node)
{
	struct bt_node *head;
	struct bt_node *tmp_bt_node;

	head = p_node->bt_queue;

	if (p_node->input_num > FD_MAX_BT_NUM) {
		tmp_bt_node = list_next_entry(head, list);
		list_del(head->list.next);

		kfree(tmp_bt_node->bt->symbol);
		kfree(tmp_bt_node->bt);
		kfree(tmp_bt_node);
	}

	list_add_tail(&new_bt_node->list, &head->list);
}

static void fd_queue_clear(struct bt_node *node)
{
	struct bt_node *tmp_bt_node;
	struct list_head *list_p, *list_temp;

	list_for_each_safe(list_p, list_temp, &node->list) {
		tmp_bt_node = list_entry(list_p, struct bt_node, list);
		if (tmp_bt_node) {
			list_del(&tmp_bt_node->list);
			kfree(tmp_bt_node->bt->symbol);
			kfree(tmp_bt_node->bt);
			kfree(tmp_bt_node);
		}
	}

	kfree(node);
}

static int fd_hash_insert(pid_t pid, unsigned int fd_type,
		unsigned int fd, struct bt_node *new_bt_node)
{
	struct fd_pro_node *new_node;
	struct fd_pro_node *head;

	struct fd_pro_node *tmp_node;
	struct list_head *list_p;

	struct bt_node *bt_head;

	unsigned int hash_key;

	if (!pid)
		return 0;

	hash_key = pid%FD_HASH_SIZE;

	mutex_lock(&hash_lock);
	if (!hash_array[hash_key]) {
		/* Allocation dumy node for list head. There is not data. */
		head = (struct fd_pro_node *)kzalloc(sizeof(struct fd_pro_node), GFP_KERNEL);

		if (!head)
			goto err1;

		head->pid = 0;
		head->input_num = 0;
		head->bt_queue = NULL;
		INIT_LIST_HEAD(&head->list);

		/* Allocation data node. */
		new_node = (struct fd_pro_node *)kzalloc(sizeof(struct fd_pro_node), GFP_KERNEL);

		if (!new_node)
			goto err2;

		new_node->pid = pid;
		new_node->input_num = 1;

		/* Allocation for bt_head. */
		bt_head = (struct bt_node *)kzalloc(sizeof(struct bt_node), GFP_KERNEL);

		if (!bt_head)
			goto err3;

		bt_head->fd = 0;
		bt_head->fd_type = 0;
		bt_head->bt = NULL;
		INIT_LIST_HEAD(&bt_head->list);

		new_node->bt_queue = bt_head;

		hash_array[hash_key] = head;
		list_add_tail(&new_node->list, &head->list);
		/* add the new_bt_node */
		fd_enqueue(new_node, new_bt_node);
		goto success;
	}

	head = hash_array[hash_key];

	list_for_each(list_p, &head->list) {
		tmp_node = list_entry(list_p, struct fd_pro_node, list);
		if (tmp_node && tmp_node->pid == pid) {
			tmp_node->input_num++;
			fd_enqueue(tmp_node, new_bt_node);
			goto success;
		}
	}

	new_node = (struct fd_pro_node *)kzalloc(sizeof(struct fd_pro_node), GFP_KERNEL);

	if (!new_node)
		goto err1;

	new_node->pid = pid;
	new_node->input_num = 1;

	/* Allocation for bt_head. */
	bt_head = (struct bt_node *)kzalloc(sizeof(struct bt_node), GFP_KERNEL);

	if (!bt_head)
		goto err3;

	bt_head->fd = 0;
	bt_head->fd_type = 0;
	bt_head->bt = NULL;
	INIT_LIST_HEAD(&bt_head->list);

	new_node->bt_queue = bt_head;
	list_add_tail(&new_node->list, &head->list);
	fd_enqueue(new_node, new_bt_node);

success:
	mutex_unlock(&hash_lock);
	return 1;

err3:
	kfree(new_node);
err2:
	kfree(head);
err1:
	mutex_unlock(&hash_lock);
	return 0;

}

static int fd_hash_delete(pid_t pid)
{
	struct fd_pro_node *head;
	struct fd_pro_node *tmp_node;
	struct list_head *list_p, *list_temp;
	unsigned int hash_key;

	if (!pid)
		return 0;

	hash_key = pid%FD_HASH_SIZE;

	mutex_lock(&hash_lock);

	head = hash_array[hash_key];
	if (!head) {
		mutex_unlock(&hash_lock);
		return 0;
	}

	list_for_each_safe(list_p, list_temp, &head->list) {
		tmp_node = list_entry(list_p, struct fd_pro_node, list);
		if (tmp_node && tmp_node->pid == pid) {
			list_del(&tmp_node->list);
			fd_queue_clear(tmp_node->bt_queue);
			kfree(tmp_node);
			break;
		}
	}

	if (list_empty(&head->list)) {
		kfree(head);
		hash_array[hash_key] = NULL;
	}

	mutex_unlock(&hash_lock);
	return 0;
}

static void kdbg_fd_print_bt(struct bt_node *local_bt_node)
{
	int i = 0;
	struct kdbg_bt_buffer *tmp_bt_buffer = NULL;

	if (!local_bt_node)
		return;

	if (!(local_bt_node->bt))
		return;

	tmp_bt_buffer = local_bt_node->bt;

	PRINT_KD("\nFD Number: %u\n", local_bt_node->fd);
#ifdef CONFIG_SMP
	PRINT_KD("Pid: %d, Tid: %d, comm: %20s[%d]\n",
			tmp_bt_buffer->pid, tmp_bt_buffer->tid, tmp_bt_buffer->comm,
			tmp_bt_buffer->cpu_number);
#else
	PRINT_KD("Pid: %d, Tid: %d, comm: %20s\n", tmp_bt_buffer->pid,
			tmp_bt_buffer->tid, tmp_bt_buffer->comm);
#endif

	for (i = 0 ; i < tmp_bt_buffer->nr_entries ; i++) {
		PRINT_KD("#%d  0x%08lx in %s () from %s\n",
				i+1,
				(tmp_bt_buffer->symbol)[i].addr,
				(tmp_bt_buffer->symbol)[i].sym_name,
				(tmp_bt_buffer->symbol)[i].lib_name);
	}

}

void kdbg_fd_save_bt(unsigned int fd_type, unsigned int fd, int save_bt_flag)
{
	struct bt_node *new_bt_node;
	int ret;

	new_bt_node = fd_bt_queue_init(fd_type, fd);
	if (!new_bt_node)
		return;

	fd_get_bt(current->pid, new_bt_node);

#ifdef CONFIG_KDEBUGD_AGENT_SUPPORT
	if (new_bt_node->bt && save_bt_flag & FD_BT_SEND_BIT) {
		/* Send BT data to TVis*/
		struct kdbg_bt_buffer *bt_buf = new_bt_node->bt;
		struct kdbg_proc_fd_bt bt = {0};
		int i = 0;

		bt.tgid = bt_buf->pid;
		bt.type = fd_type;
		for (i = 0 ; i < bt_buf->nr_entries
				&& i < 7; i++) {
			bt.pc[i] = bt_buf->symbol[i].addr;
		}
#ifdef KDBG_FD_DEBUG
		PRINT_KD("%s:%u pid (%d) Type %d\n", __FILE__, __LINE__, bt.tgid, bt.type);
		PRINT_KD("%s:%u BT  <%u>\n"
				"<%u>\n"
				"<%u>\n"
				"<%u>\n"
				"<%u>\n"
				"<%u>\n"
				"<%u>\n", __FILE__, __LINE__,
				bt.pc[0], bt.pc[1], bt.pc[2], bt.pc[3], bt.pc[4], bt.pc[5], bt.pc[6]);
#endif
		agent_write(KDBG_CMD_PROC_FD_BT, &bt, sizeof(struct kdbg_proc_fd_bt));
	}
#endif

	if (save_bt_flag & FD_BT_SAVE_BIT) {
		ret = fd_hash_insert(current->tgid, fd_type, fd, new_bt_node);
		if (!ret) {
			PRINT_KD("kdebugd FD Tracer: backtrace input fail!\n");
			goto fail;
		}
		return;
	}

fail:
	kfree(new_bt_node->bt->symbol);
	kfree(new_bt_node->bt);
	kfree(new_bt_node);
	return;
}

void kdbg_fd_clear(pid_t pid)
{
	fd_hash_delete(pid);
}

void kdbg_fd_overflow(pid_t pid)
{
	struct fd_pro_node *head;
	struct fd_pro_node *tmp_node = NULL;

	struct bt_node *tmp_bt_node;
	struct list_head *list_p;

	unsigned int hash_key;

	if (!pid)
		return;

	hash_key = pid%FD_HASH_SIZE;

	mutex_lock(&hash_lock);

	if (!hash_array[hash_key]) {
		mutex_unlock(&hash_lock);
		PRINT_KD(KERN_ALERT "kdebugd FD Tracer: No Data!\n");
		return;
	}

	head = hash_array[hash_key];

	list_for_each(list_p, &head->list) {
		tmp_node = list_entry(list_p, struct fd_pro_node, list);
		if (tmp_node && tmp_node->pid == pid)
			break;
	}

	if (tmp_node) {
		list_for_each(list_p, &tmp_node->bt_queue->list) {
			tmp_bt_node = list_entry(list_p, struct bt_node, list);
			kdbg_fd_print_bt(tmp_bt_node);
		}
	}

	mutex_unlock(&hash_lock);
}

static void kdbg_fd_print_process_info(pid_t pid)
{

	struct task_struct *tsk = NULL;
	struct fdtable *fdt;
	struct files_struct *tsk_files_pt = NULL;

	PRINT_KD(KERN_ALERT "pid : %u\n", pid);

	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk) {
		/*Increment usage count */
		get_task_struct(tsk);
	}
	/*Unlock */
	rcu_read_unlock();

	if (!tsk) {
		PRINT_KD("Invalid task ID\n");
		return;
	}
	tsk_files_pt = get_files_struct(tsk);
	if (tsk_files_pt == NULL) {
		PRINT_KD("Err:Task files pointer is NULL\n");
		put_task_struct(tsk);
		return;
	}

	fdt = files_fdtable(tsk->files);
	if (fdt == NULL) {
		PRINT_KD("Err:File table pointer is NULL\n");
		put_files_struct(tsk_files_pt);
		put_task_struct(tsk);
		return;
	}

	PRINT_KD("-----------------------------------------------------\n");
	PRINT_KD("Number of open fd : %d\n", fdt->cur_fdn);
	PRINT_KD("-----------------------------------------------------\n");
	PRINT_KD("  file type : %u\n", fdt->file_fdn);
	PRINT_KD("socket type : %u\n", fdt->socket_fdn);
	PRINT_KD("  pipe type : %u\n", fdt->pipe_fdn);
	PRINT_KD("        etc : %u\n", fdt->etc_fdn);
	PRINT_KD("-----------------------------------------------------\n\n");

	put_files_struct(tsk_files_pt);
	put_task_struct(tsk);
}

static void kdbg_fd_overflow_info(void)
{
	struct fd_pro_node *head;
	struct fd_pro_node *tmp_node;
	struct list_head *list_p;

	int p_count = 0;
	int i = 0;

	mutex_lock(&hash_lock);
	for (i = 0 ; i < FD_HASH_SIZE ; i++) {
		if (hash_array[i]) {
			head = hash_array[i];
			list_for_each(list_p, &head->list) {
				tmp_node = list_entry(list_p, struct fd_pro_node, list);
				if (tmp_node) {
					kdbg_fd_print_process_info(tmp_node->pid);
					p_count++;
				}
			}
		}
	}
	mutex_unlock(&hash_lock);

	if (!p_count)
		PRINT_KD("\nThere is no dangerous process.\n");
}
#endif
