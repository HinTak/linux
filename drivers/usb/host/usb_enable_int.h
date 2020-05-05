#ifndef __USB_ENABLE_INT__
#define __USB_ENABLE_INT__


#define USB_SET_HIGH			1
#define USB_SET_LOW				0

#define OCM_PLUG				1
#define OCM_UNPLUG				0

#define MICOM_NORMAL_DATA		1
#define KEY_PACKET_DATA_SIZE	1

#define MICOM_MAX_CTRL_TRY		3
#define OCM_MAX_CTRL_TRY		10

#define TVKEY_RESET_TIME		500		// msec

#define	BUS_NUM_MASK			0xFFFF
#define	PORT_NUM_MASK			0xFFFF
#define	PORT_NUM_SHIFT			16

#if defined(__KANTM_REV_0__)			// Kant.M

#define TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_1	2
#define TVKEY_KANTM_BUILTIN_SUPPORT_USB_BUS_2	3
#define TVKEY_KANTM_OCM_SUPPORT_USB_BUS			2
#define TVKEY_KANTM_OC_SUPPORT_USB_BUS			2

#elif defined(__KANTM_REV_1__)			// Kant.M2

#define TVKEY_OCL_USB			2
#define TVKEY_BUILTIN_USB_1		2
#define TVKEY_BUILTIN_USB_2		3

#endif

//KANT M, 2016
#define OCM_USB_ENABLE			15
//KANT M2 OCL, 2017
#define OCL_USB_ENABLE_1		18
#define OCL_USB_ENABLE_2		14


/*
// It was used for Jazz.M only.
// This is not used but reserve for the OS upgrade.
#define OCM_USB_ENABLE					6
#define TVKEY_JAZZM_SUPPORT_USB_BUS		3
*/


static int gpio;
static struct cdev usben_gpio_cdev;
static int current_gpio_val;
static dev_t usben_id;
static int *read_status;
struct class *device_class;
int usben_probe_done;


#define USB_EXT_SYMBOL(fp, ret, FUNCTION, ARGS...) \
do \
{ \
	typeof(ret) __r = -EINVAL; \
	typeof(&FUNCTION) __a = fp; \
	if (__a) { \
		__r = (int) __a(ARGS); \
	} else { \
		__a = symbol_request(FUNCTION); \
		fp = __a; \
		if (__a) { \
			__r = (int) __a(ARGS); \
			symbol_put(FUNCTION); \
		} else { \
			pr_err("cannot get " \
			"symbol "#FUNCTION"()\n"); \
		} \
	} \
	ret = __r; \
} while (0)


extern int tztv_hdmi_isOCMConnected;

extern int sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len);
extern int sdp_ocm_gpio_write(unsigned int port, unsigned int level);
extern int sdp_ocm_gpio_read(unsigned int port);
extern int sdp_ocm_dongle_onoff(unsigned int onoff);
extern int tztv_sys_is_oc_support(void);
extern int tztv_sys_is_ocm_model(void);
extern int tztv_sys_get_platform_info(void);	// 0 == M2e, 2 == M2, 3 == M2s(OCL) 4 == OCL Frame
extern int tztv_sys_get_pcb_info(void);		// 3 == M3s OCL 0Â÷ º¸µå

int _ext_sdp_micom_send_cmd_ack(char cmd, char ack, char *data, int len)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_micom_send_cmd_ack, cmd, ack, data, len);

	return ret;
}


int _ext_sdp_ocm_gpio_write(unsigned int port, unsigned int level)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_gpio_write, port, level);

	return ret;
}


int _ext_sdp_ocm_gpio_read(unsigned int port)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_gpio_read, port);

	return ret;
}

int _ext_sdp_ocm_dongle_onoff(unsigned int onoff)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, sdp_ocm_dongle_onoff, onoff);

	return ret;
}



int _ext_tztv_sys_is_oc_support(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_is_oc_support);

	return ret;
}


int _ext_tztv_sys_is_ocm_model(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_is_ocm_model);

	return ret;
}


int _ext_tztv_sys_get_platform_info(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_get_platform_info);

	return ret;
}


int _ext_tztv_sys_get_pcb_info(void)
{
	int ret = 0;
	static void *ext_fp = NULL;

	USB_EXT_SYMBOL(ext_fp, ret, tztv_sys_get_pcb_info);

	return ret;
}


#endif

