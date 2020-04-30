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
#include <linux/pm.h>
#include <linux/proc_fs.h>

#ifdef CONFIG_VD_RELEASE
#define DEBUG_SHELL	0
#else
#define DEBUG_SHELL	1
#endif

extern void __thaw_task(struct task_struct *p);
extern void machine_restart_standby(char *cmd);

#define STR2(x)	#x
#define STR(X)	STR2(X)

#define PWSV_MODE		"pwsv_mode"
#define PWSV_MINOR		253
#define CMD_BUFSIZE		256

const char *pwsv_support_comm[] =
{
	"vd-net-config",	/* Wow Hub Process */
	"net-config",
	"wpa_supplicant",	/* Network Process */
	"connm",
	"save_error",
	"gdbus",
	"dbus-",
	"khubd",		/* USB connection Process */
	"instant",
	"user_port_threa",
	"udevd",			/* udevd Process */
	"buxton2d",		/* for dbus */
	"cynara",		/* for dbus */
};

/*
  pwsv : array of pwsv_info for each mode
*/
struct pwsv_info pwsv[PWSV_MODE_MAX] =
{
	{"NONE", },
	{"IOT-HUB", },
	{"ART-APP", },
};

/*
  pwsv_mode_flags : bitmap information that enabled each mode
   0010b : IOT
   0100b : INACTIVE
   ...
*/
int pwsv_mode_flags = 0x0;

/*
  pwsv_current_mode : current running mode about power saving mode
*/
int pwsv_current_mode = PWSV_MODE_TV;
EXPORT_SYMBOL(pwsv_current_mode);

/*
  pwsv_wakeup_state : has wakeup state
*/
int pwsv_wakeup_state = 0;

/*
  pwsv_freezing : It is freezing
*/
bool pwsv_freezing = false; /* for frezzer */

extern unsigned int *micom_gpio;
#if defined(CONFIG_ARCH_SDP1404)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
#define ALIVE_MICOM		0x18
#define SUS_MICOM		0x25
int pwsv_get_boot_reason_micom(void)
{
	int ret=0;
	unsigned int * p_gpio0, * p_gpio1 , * p_gpio2;
	unsigned int save_reg0 , save_reg1 , save_reg2;
	if(!pwsv_mode_flags)
		return PWSV_MODE_TV;

	/* US P 0.1 */
	p_gpio0 = micom_gpio + 0x465;
	save_reg0 = *(volatile unsigned int*)p_gpio0;
	*(volatile unsigned int*)p_gpio0 |= (0x2 << 4);
	/* US P 2.3 */
	p_gpio1 = micom_gpio + 0x486;
	save_reg1 = *(volatile unsigned int*)p_gpio1;
	*(volatile unsigned int*)p_gpio1 |= (0x2 << 12);
	/* US P 2.4 */
	p_gpio2 = micom_gpio + 0x486;
	save_reg2 = *(volatile unsigned int*)p_gpio2;
	*(volatile unsigned int*)p_gpio2 |= (0x2 << 16);

	p_gpio0 = micom_gpio + 0x467;
	ret |= ((*(volatile unsigned int*)p_gpio0>>1) & 1) << 2;

	p_gpio1 = micom_gpio + 0x488;
	ret |= ((*(volatile unsigned int*)p_gpio1>>3) & 1) << 1;

	p_gpio2 = micom_gpio + 0x488;
	ret |= ((*(volatile unsigned int*)p_gpio2>>4) & 1);

	*(volatile unsigned int*)p_gpio0 = save_reg0;
	*(volatile unsigned int*)p_gpio1 = save_reg1;
	*(volatile unsigned int*)p_gpio2 = save_reg2;

	return ret;
}

#elif defined(CONFIG_ARCH_SDP1406)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
int pwsv_get_boot_reason_micom(void)
{
	int ret=0;
	unsigned int * p_gpio;
	if(!pwsv_mode_flags)
		return PWSV_MODE_TV;

	/* MICOM register */
	p_gpio = micom_gpio;
	ret = *(volatile unsigned int*)p_gpio;

	if( ret > PWSV_MODE_TV) return pwsv_current_mode;
	else return ret;
}

#elif defined(CONFIG_ARCH_SDP1501) || defined(CONFIG_ARCH_SDP1601)
extern int sdp_cpufreq_freq_fix(unsigned int freq, bool on);
int pwsv_get_boot_reason_micom(void)
{
	int ret=0;
	unsigned int * p_gpio;

	/* MICOM register */
	p_gpio = micom_gpio;
	ret = *(volatile unsigned int*)(p_gpio+2);
	switch(ret)
	{
		case BOOT_TYPE_WOWLAN_PULSE7:
		case BOOT_TYPE_ZIGBEE:
		case BOOT_TYPE_E_PHY_ANYPACKET:
			return pwsv_current_mode; /* need to wakeup? */
		case BOOT_TYPE_IOT_KEEP_ALIVE:
			return pwsv_current_mode;
		case BOOT_TYPE_PANEL_POWER_KEY:
			return PWSV_BOOTREASON_COLD_REBOOT;
		default:
			pr_err("PWSV] Micom Boot reason : %d\n",ret);
			return PWSV_MODE_TV;
	}
}

#elif defined(CONFIG_ARCH_NVT72172)
extern int check_stbc_power_key(void);
int pwsv_get_boot_reason_micom(void)
{
	if (check_stbc_power_key())
		return PWSV_MODE_TV;

	return pwsv_current_mode;
}
#else
int pwsv_get_boot_reason_micom(void)
{
	return PWSV_MODE_TV;
}
#endif

void pwsv_reboot(int pwsvmode)
{
	pr_err("PWSV] timed out(%d) - pwsv_reboot\n", pwsvmode);
#ifdef CONFIG_VD_RELEASE
	/* SAVE ERROR LOG */
	set_kpi_fault(0, 0, pwsv[pwsvmode].name, pwsv[pwsvmode].name, "PWSV", -1/* tgid is unnecessary */);
	machine_restart_standby("PWSV REBOOT");
#endif
}

/* 
 Check if pwsv mode is running or not.
  Return value
   0     : Not running
   n > 1 : Specific mode
*/ 
int pwsv_is_running(void)
{
	if(pwsv_current_mode > PWSV_MODE_TV)
		return pwsv_mode_flags;
	else
		return 0;
}
EXPORT_SYMBOL(pwsv_is_running);

void pwsv_init_wdt_counter(int counter)
{
	int i = 0;
	for(i = PWSV_MODE_NONE ; i < PWSV_MODE_MAX ; i++) {
		spin_lock(&pwsv[i].pwsv_wdt_lock);
		pwsv[i].pwsv_wdt_cnt = counter;
		pwsv[i].pwsv_last_wdt_cnt = counter;
		spin_unlock(&pwsv[i].pwsv_wdt_lock);
	}
}
EXPORT_SYMBOL(pwsv_init_wdt_counter);

int pwsv_read_counter(int mode)
{
	return pwsv[mode].pwsv_wdt_cnt;
}
EXPORT_SYMBOL(pwsv_read_counter);

int pwsv_check_wdt(int count)
{
	int i = 0;
	for(i = PWSV_MODE_NONE ; i < PWSV_MODE_MAX ; i++)
	{
		if(pwsv_mode_flags & (1 << i))	/* find enabled mode */
		{
			if(pwsv[i].pwsv_last_wdt_cnt < pwsv_read_counter(i))
			{
				pwsv[i].pwsv_last_wdt_cnt = pwsv_read_counter(i);
				return -1;		/* signal continued from App */
			} else {
				if(count > PWSV_WDT_TIMEOUT)
					return i;	/* watchdog timeout */
			}
		}
	}
	return 0; // no signal but not timed out yet
}
EXPORT_SYMBOL(pwsv_check_wdt);

int pwsv_check_wakeup_req(void) {
	int boot_reason_micom = pwsv_get_boot_reason_micom();
	
	if((boot_reason_micom == PWSV_BOOTREASON_COLD_REBOOT)
		|| (boot_reason_micom == PWSV_MODE_TV)){
		pwsv_wakeup_state = boot_reason_micom;
		return pwsv_wakeup_state;
	}

	return 0; /* Wakeup is not requested */
}
EXPORT_SYMBOL(pwsv_check_wakeup_req);

void init_white_list(void);
void pwsv_init_wakeup_state(void) {
	pwsv_wakeup_state = PWSV_MODE_DEFAULT;
	init_white_list();
	return;
}
EXPORT_SYMBOL(pwsv_init_wakeup_state);

int pwsv_get_wakeup_state(void) {
	return pwsv_wakeup_state;
}
EXPORT_SYMBOL(pwsv_get_wakeup_state);

void pwsv_print_status(int cnt) {
	if(cnt % (1*1000/PWSV_TIMESLICE) == 0) /* 1s/50ms */
	{
		pr_err("PWSV] mode : %d\n", pwsv_current_mode);
	}
	return;
}
EXPORT_SYMBOL(pwsv_print_status);

inline void pwsv_increase_alive(int mode)
{
	spin_lock(&pwsv[mode].pwsv_wdt_lock);
	pwsv[mode].pwsv_wdt_cnt++;
	spin_unlock(&pwsv[mode].pwsv_wdt_lock);
}

void pwsv_reset_thaw_pids(void)
{
	struct pwsv_pid *pid = NULL, *tmp = NULL;
	int i = 0;
	for(i = PWSV_MODE_NONE ; i < PWSV_MODE_MAX ; i++) {
		spin_lock(&pwsv[i].pwsv_thaw_pid_list_lock);
		list_for_each_entry_safe(pid, tmp, &pwsv[i].pwsv_thaw_pid_list, list) {
			list_del(&pid->list);
			kfree(pid);
		}
		INIT_LIST_HEAD(&pwsv[i].pwsv_thaw_pid_list);
		spin_unlock(&pwsv[i].pwsv_thaw_pid_list_lock);
	}
}

void pwsv_clear_suspend_mode(void)
{
	pwsv_mode_flags &= ~(1 << PWSV_SUSPEND);
}

static ssize_t pwsv_read(struct file *filp, char __user *user_buf,
                size_t size, loff_t *ppos)
{
	char kbuf[16];
	int ret=0;
	int len;

	len = snprintf(kbuf,sizeof(kbuf),"%d\n",pwsv_current_mode);

	ret = simple_read_from_buffer(user_buf, size, ppos, kbuf, len);
	return ret;
}

#ifdef CONFIG_SDP_MICOM_MSGBOX
extern int sdp_messagebox_write(unsigned char* pbuffer, unsigned int size);

static char checksum_cal(const char *buf, int msg_len)
{
	char checksum = 0x00;
	int index = 0;

	if (buf == NULL)
		return checksum;

	for (index = 0; index < msg_len; index++)
		checksum += buf[index];
	
	return checksum;
}

void pwsv_notice_pwsvmode_submicom(int mode_flags)
{
	unsigned char pbuffer[9];

	pbuffer[0] = 0xff;		// Submicom packet(Fixed)
	pbuffer[1] = 0xff;		// Submicom normal packet
	pbuffer[2] = 0x36;		// CTM_ONOFF_CONTROL
	pbuffer[3] = 0x00;		// 0x0017: MICOM_ON_WORKING_POWER_SAVING_MODE
	pbuffer[4] = 0x17;	
	pbuffer[5] = (mode_flags&0xFF00)>>8;	// mode_flags
	pbuffer[6] = mode_flags&0xFF;		// mode_flags: 0x02(IOT), 0x04(INACTIVE)
	pbuffer[7] = 0x00;		// not used

	pbuffer[8] = checksum_cal((const char*)&pbuffer[2], 6);
	pr_info("[%03d,%s] [Send][0x%02X][0x%02X][0x%02X][0x%02X][0x%02X][0x%02X][0x%02X][0x%02X][0x%02X]\n", 
			current->pid, current->comm, pbuffer[0],pbuffer[1],pbuffer[2],pbuffer[3],pbuffer[4],pbuffer[5],pbuffer[6],pbuffer[7],pbuffer[8]);
	sdp_messagebox_write(pbuffer, 9);
}
#endif

void init_white_list(void)
{
	struct task_struct *p, *g;
	int i, j;
	struct pwsv_pid *init_pid, *pid;
	int predefined_proc_cnt = sizeof(pwsv_support_comm)/sizeof(pwsv_support_comm[0]);
	int registered = 0;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		for(i = 0 ; i < predefined_proc_cnt ; i++)
		{
			/* if found pre-defined process */
			if(strstr(p->comm, pwsv_support_comm[i]) != 0)
			{
				for(j = PWSV_MODE_MAX-1 ; j > PWSV_MODE_NONE ; j--)
				{
					/* for current mode pid list */
					if(pwsv_mode_flags & (1 << j))
					{
						/* check if it is already registered */
						list_for_each_entry(pid, &pwsv[j].pwsv_thaw_pid_list, list)
						{
							if(pid->tgid == p->tgid)
							{
								registered = true;
								break;
							}
						}
						break;
					}
				}

				if(!registered) {
					init_pid = kmalloc(sizeof(*init_pid), GFP_KERNEL);
					init_pid->tgid = p->tgid;
					spin_lock(&pwsv[j].pwsv_thaw_pid_list_lock);
					list_add_tail(&init_pid->list, &pwsv[j].pwsv_thaw_pid_list);
					spin_unlock(&pwsv[j].pwsv_thaw_pid_list_lock);
					break;
				}
				registered = false;
			}
		}
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}

void pwsv_thaw_threads(suspend_state_t state)
{
	struct task_struct *p, *g;
	struct pwsv_pid *pid, *tmp;
	struct list_head *head;
	int i;

	read_lock(&tasklist_lock);

	do_each_thread(g, p) {

		if(strcmp(p->comm, "vd-net-config") == 0)
			send_sig(SIGUSR2, p, 0 /*SEND_SIG_NOINFO*/ );

		for(i = PWSV_MODE_NONE ; i < PWSV_MODE_MAX ; i++) {
			if(pwsv_mode_flags & (1 << i)) {
				list_for_each_entry_safe(pid, tmp, &pwsv[i].pwsv_thaw_pid_list, list) {
					if(p->tgid == pid->tgid) {
						pr_warning("thaw thread [%d(%s)]\n", p->tgid, p->comm);
						__thaw_task(p);
					}
				}
			}
		}

#if DEBUG_SHELL
		if(strcmp(p->comm,"sh") == 0 ||
			strstr(p->comm,"dlogutil") != 0 ||
				strstr(p->comm,"vddebugmenu") != 0) {
			pr_warning("thaw thread [%d(%s)]\n", p->tgid, p->comm);
			__thaw_task(p);
		}
#endif /* DEBUG_SHELL */
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}
EXPORT_SYMBOL(pwsv_thaw_threads);

static int pwsv_parse_subcmd(char* cmd, int mode)
{
        struct task_struct *p, *g, *ns;
	struct pwsv_pid *pid, *new_pid;
        int tgid;

	if(strncmp(cmd, PWSV_SUBCMD_ON, strlen(PWSV_SUBCMD_ON)) == 0) {
		pwsv_mode_flags |= (1 << mode);
	}
	else if(strncmp(cmd, PWSV_SUBCMD_OFF, strlen(PWSV_SUBCMD_OFF)) == 0) {
		pwsv_mode_flags &= ~(1 << mode);
	}
	else if(strncmp(cmd, PWSV_SUBCMD_ALIVE, strlen(PWSV_SUBCMD_ALIVE)) == 0) {
                pwsv_increase_alive(mode);
	}
	else {
		if(sscanf(cmd,"%" STR(CMD_BUFSIZE) "d", &tgid) != 1)
			return -EFAULT;

		if(tgid != 0) {
			ns = find_task_by_vpid((pid_t)tgid);
			if(!ns)
				return -EFAULT;
			tgid = ns->tgid;

			list_for_each_entry(pid, &pwsv[mode].pwsv_thaw_pid_list, list) {
				if(pid->tgid == tgid)
				{
					pr_err("PWSV] %d(%s) is already registered\n", tgid, ns->comm);
					return true;
				}
			}
			read_lock(&tasklist_lock);
			do_each_thread(g, p){
				if(p->tgid == tgid){
					new_pid = kmalloc(sizeof(*new_pid), GFP_KERNEL);
					new_pid->tgid = tgid;
					spin_lock(&pwsv[mode].pwsv_thaw_pid_list_lock);
					list_add_tail(&new_pid->list, &pwsv[mode].pwsv_thaw_pid_list);
					spin_unlock(&pwsv[mode].pwsv_thaw_pid_list_lock);
					pr_err("PWSV] set Process : %d(tgid) (%s)\n", p->tgid, p->comm);
					read_unlock(&tasklist_lock);
					goto out;
				}
			}while_each_thread(g, p);
			read_unlock(&tasklist_lock);
		}	
	}

out:
        return true;
}

static int pwsv_parse_cmd(char* cmd1, char* cmd2)
{
	int err;

	if((cmd1 == NULL) || (cmd2 == NULL)) {
		return -EINVAL;
	}

	if(strncmp(cmd1, PWSV_CMD_INACTIVE, strlen(PWSV_CMD_INACTIVE)) == 0){
		err = pwsv_parse_subcmd(cmd2, PWSV_INACT);
		if(err < 0)
			return err;
        }
        else if(strncmp(cmd1, PWSV_CMD_IOT, strlen(PWSV_CMD_IOT)) == 0){
		err = pwsv_parse_subcmd(cmd2, PWSV_IOT);
		if(err < 0)
			return err;
        }
	else if(strncmp(cmd1, PWSV_CMD_SUSPEND, strlen(PWSV_CMD_SUSPEND)) == 0){
		err = pwsv_parse_subcmd(cmd2, PWSV_SUSPEND);
		if(err < 0)
			return err;
        }
	//else common pid??

        return true; /* finish job with 1st command */
}

static ssize_t pwsv_write(struct file *filp, const char __user *user_buf,
                size_t size, loff_t *ppos)
{
        char set_data[CMD_BUFSIZE];
        int buf_size;
	char *cmd1, *cmd2;
        int ret;

	buf_size = min(size,(sizeof(set_data)-1));
        ret = simple_write_to_buffer(set_data, buf_size, ppos, user_buf, buf_size);
        if (ret != size) {
                return ret >= 0 ? -EFAULT : ret;
        }

        set_data[buf_size] = 0;
        cmd2 = set_data;
        cmd1 = strsep(&cmd2, " ");

        ret = pwsv_parse_cmd(cmd1, cmd2);
	if(ret < 0)
		return ret;

	return size;
}

static int pwsv_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations pwsv_fops = {
	.open  = pwsv_open,
	.read  = pwsv_read,
	.write = pwsv_write,
};

static struct miscdevice pwsvmode_dev = {
	.minor = PWSV_MINOR,
	.name = PWSV_MODE,
#ifdef CONFIG_SECURITY_SMACK_SET_DEV_SMK_LABEL
	.lab_smk64 = "*",
#endif
	.fops = &pwsv_fops,
	.mode = 0777,
};

static int __init pwsv_init(void)
{
	int ret = 0;
	int i;

	ret = misc_register(&pwsvmode_dev);

	if (ret)
		pr_err("PWSV] Failed to regist miscellaneous device\n");

	for(i = PWSV_MODE_NONE ; i < PWSV_MODE_MAX ; i++) {
		spin_lock_init(&pwsv[i].pwsv_thaw_pid_list_lock);
		spin_lock_init(&pwsv[i].pwsv_wdt_lock);
		INIT_LIST_HEAD(&pwsv[i].pwsv_thaw_pid_list);
	}

	return 0;
}

static void __exit pwsv_exit(void)
{
	misc_deregister(&pwsvmode_dev);
	return;
}
module_init(pwsv_init);
module_exit(pwsv_exit);
