
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/kernel_stat.h>
#ifdef CONFIG_VD_RELEASE
#define DEBUG_SHELL	0
#else
#define DEBUG_SHELL	1
#endif

extern void set_kpi_fault(unsigned long pc, unsigned long lr, char *thread_name, char *process_name, char *type);
extern bool freeze_task(struct task_struct *p);
extern void __thaw_task(struct task_struct *p);
extern void machine_restart_standby(char *cmd);

#define NO_IOT_THREAD		1
#define iotnode_NAME            "iotnode"
#define iotnode_MINOR           254
#define IOT_MAX			256
#define OFF_MODE        0
#define TV_MODE         1
#define SEND_BUFSIZE	256

/* IOT COMMAND */
#define ON_COMMAND		"ON"
#define OFF_COMMAND		"OFF"
#define SUSPEND_COMMAND		"SUSPEND"
#define TV_MODE_COMMAND		"TV_MODE"
#define ALIVE_COMMAND		"ALIVE"
#define REBOOT_COMMAND		"REBOOT"

static DEFINE_SPINLOCK(iot_wdt_lock);
int iot_wathdog_counter = 0;
bool iot_onoff = 0;
int iotmode = TV_MODE;
int transit_to_tvmode = 0;
int iot_pid = 0;
int iot_thread_list[IOT_MAX];
EXPORT_SYMBOL(iotmode);
int wakeup_iot = 0;

void iot_reboot(int pid, char *process_name)
{
	char reboot_cmd[SEND_BUFSIZE];

	printk(KERN_ERR"IOT] iot_reboot by %s\n", process_name);
	snprintf(reboot_cmd, SEND_BUFSIZE,"Z %d %s%c",pid, process_name, '\0');
#ifdef CONFIG_VD_RELEASE
	/* SAVE ERROR LOG */
	set_kpi_fault(0, 0, "IOT-HUB", process_name, "IOT-HUB");
	machine_restart_standby("IOT REBOOT");
#endif
}

void set_iot_counter(int counter)
{
	spin_lock(&iot_wdt_lock);
	iot_wathdog_counter = counter;
	spin_unlock(&iot_wdt_lock);
}
EXPORT_SYMBOL(set_iot_counter);

int read_iot_counter(void)
{
	return iot_wathdog_counter;
}
EXPORT_SYMBOL(read_iot_counter);

inline void iot_alive_signal(void)
{
	spin_lock(&iot_wdt_lock);
	iot_wathdog_counter++;
	spin_unlock(&iot_wdt_lock);
}

static ssize_t iotnoderead(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	char kbuf[256];

	snprintf(kbuf,256,"%d",iotmode);

	if(copy_to_user(buf,kbuf,256)){
		return -EFAULT;
	} 

	return 1;
}

#ifdef CONFIG_SDP_MESSAGEBOX
extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);
#endif

void iot_thaw_thread(suspend_state_t state)
{
	struct task_struct *p, *g;
	int p_count;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	rcu_scheduler_active = 0;
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
	unsigned char pbuffer[9];

	read_lock(&tasklist_lock);

#ifdef CONFIG_SDP_MESSAGEBOX
	pbuffer[0] = 0xff;
	pbuffer[1] = 0xff;
	pbuffer[2] = 0x36;
	pbuffer[3] = 0x00;
	pbuffer[4] = 0x07; // OCM power sw
	pbuffer[5] = 0x00;
	pbuffer[6] = 0x00;
	pbuffer[7] = 0x00;
	pbuffer[8] = 0x3d;
	sdp_messagebox_write(pbuffer, 9);
#endif

	do_each_thread(g, p){
		/* Wow Hub Process */
		if(strstr(p->comm,"vd-net-config")!=0){
			send_sig(SIGUSR2, p, 0/*SEND_SIG_NOINFO*/);
			__thaw_task(p);
		}
		else if(strstr(p->comm,"net-config")!=0 ){
			__thaw_task(p);
		}

		for(p_count = 0; p_count < iot_pid ; p_count++)
			if(p->tgid == iot_thread_list[p_count]){
				__thaw_task(p);
			}

		/* Network Process */
		if(strstr(p->comm,"wpa_supplicant")!=0 || strstr(p->comm,"connm")!=0 || strstr(p->comm,"save_error")!=0 || strstr(p->comm,"gdbus")!=0 || strstr(p->comm,"dbus-")!=0){
			__thaw_task(p);
		}

		/* USB connection Process */
		if(strstr(p->comm,"khubd")!=0 || strstr(p->comm,"instant")!=0 || strstr(p->comm,"user_port_threa")!=0){
			__thaw_task(p);
		}

#if DEBUG_SHELL
		if(strcmp(p->comm,"sh") == 0 || strstr(p->comm,"dlogutil") != 0  ){
			__thaw_task(p);
		}
#endif /* DEBUG_SHELL */
	}while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}
EXPORT_SYMBOL(iot_thaw_thread);

static ssize_t iotnodewrite(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct task_struct *p, *g;
	int err = NO_IOT_THREAD;
	char set_data[256];
	int tgid;

	if(copy_from_user(set_data,buf,256))
		return -EFAULT;

	if(strstr(set_data,TV_MODE_COMMAND)!=0){
		wakeup_iot = TV_MODE;
	}
	else if(strstr(set_data, SUSPEND_COMMAND)!=0){
		wakeup_iot = OFF_MODE;
	}
	else if(strstr(set_data, ALIVE_COMMAND)!=0){
		iot_alive_signal();
	}
	else if(strstr(set_data, REBOOT_COMMAND)!=0){
		iot_reboot(current->pid, current->comm);
	}
	else if(strstr(set_data, ON_COMMAND)!=0){
		iot_onoff = true;
	}
	else if(strstr(set_data, OFF_COMMAND)!=0){
		iot_onoff = false;
	}else{
		sscanf(set_data,"%d",&tgid);

		if((tgid != 0) && (iot_pid < IOT_MAX)){
			read_lock(&tasklist_lock);
			do_each_thread(g, p){
				if(p->tgid == tgid){
					iot_thread_list[iot_pid] = tgid;
					iot_pid++;
					printk(KERN_INFO"IOT] set Process : %d(tgid) (%s)\n",p->tgid,p->comm);
					break;
				}  
			}while_each_thread(g, p);
			read_unlock(&tasklist_lock);
		}
	}	
	return err;
}

static int iotnodeopen(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations iotnode_fops = {
	.owner = THIS_MODULE,
	.open  = iotnodeopen,
	.read  = iotnoderead,
	.write = iotnodewrite,
};

static struct miscdevice iotnode_dev = {
	.minor = iotnode_MINOR,
	.name = iotnode_NAME,
	.fops = &iotnode_fops,
	.mode = 0777,
};

static int __init iotnode_init(void)
{
	int ret_val = 0;
	ret_val = misc_register(&iotnode_dev);

	if(ret_val){
		printk(KERN_ERR "IOT] ERR %s: misc register failed\n", iotnode_NAME);
	}
	else {
		printk(KERN_INFO"IOT] %s initialized\n", iotnode_NAME);
	}

	return ret_val;
}
static void __exit iotnode_exit(void)
{
	misc_deregister(&iotnode_dev);
	return;
}
module_init(iotnode_init);
module_exit(iotnode_exit);
