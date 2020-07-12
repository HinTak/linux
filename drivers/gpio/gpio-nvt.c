/* ********************************************************************* */
/*  File Name: ntgpio.c */
/*  Description: gpio driver */
/* ********************************************************************* */
#include <linux/kernel.h>       /* ARRAY_SIZE */
#include <linux/init.h>         /* module_init/module_exit macros */
#include <linux/module.h>       /* MODULE_LICENSE */
#include <linux/interrupt.h>    /* irqreturn_t, tasklet */
#include <linux/err.h>          /* IS_ERR */
#include <linux/semaphore.h>
#include <linux/mm.h>           /* struct vm_area_struct */
#include <linux/fs.h>           /* struct file_operations */
#include <linux/dma-mapping.h>  /* dma_ API */
#include <linux/cdev.h>         /* cdev_ API */
#include <linux/kdev_t.h>		/* MKDEV */
#include <asm/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>		/*! copy_from_user, copy_to_user */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/delay.h>          /*! mdelay, udelay */
#include <linux/timer.h>    /* timer */
#include <linux/jiffies.h>  /* jiffies */
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/ioport.h>
#include <linux/io.h>

#include "gpio-nvt.h"
#include "gpio-nvt-tbl.h"

#define DRIVER_NAME "nt726xx-gpio"
#define DRIVER_VERSION "1.0"
#define DRIVING_BYPASS 255
#if defined(CONFIG_ARCH_NVT72671D)
#define AMP_RESET_PIN 159
#endif


static LIST_HEAD(nt726xx_gpio_list);

struct nt726xx_pdata {
	int gpio_base;
};

struct nt726xx_gpio {
	struct nt726xx_gpio_dev *nt726xx;
	struct gpio_chip gpio_chip;
};


/* NT726xx GPIO device structure */
struct nt726xx_gpio_dev {
	struct mutex irq_lock;
	struct device		*dev;
	void __iomem		*base;
	int gpio_base;
	int irq_base;
	int			irq;
	struct completion	msg_complete;

	unsigned short			msg_err;	/* message errors */

	/* Used over suspend/resume */
	bool suspended;

	/* platform data */
	unsigned char		speed;
	unsigned char		filter;
};

struct gpio_regs {
	unsigned int irqenable1;
	unsigned int irqenable2;
	unsigned int wake_en;
	unsigned int ctrl;
	unsigned int oe;
	unsigned int leveldetect0;
	unsigned int leveldetect1;
	unsigned int risingdetect;
	unsigned int fallingdetect;
	unsigned int dataout;
	unsigned int debounce;
	unsigned int debounce_en;
};

struct gpio_bank {
	struct list_head node;
	void __iomem *base;
	unsigned short irq;
	unsigned int non_wakeup_gpios;
	unsigned int enabled_non_wakeup_gpios;
	struct gpio_regs context;
	unsigned int saved_datain;
	unsigned int level_mask;
	unsigned int toggle_mask;
	spinlock_t lock;
	struct gpio_chip chip;
	unsigned int mod_usage;
	unsigned int irq_usage;
	unsigned int dbck_enable_mask;
	bool dbck_enabled;
	struct device *dev;
	bool loses_context;
	bool context_valid;
	int stride;
	unsigned int width;
	int context_loss_count;
	int power_mode;
	bool workaround_enabled;

	void (*set_dataout)(struct gpio_bank *bank, int gpio, int enable);
	int (*get_context_loss_count)(struct device *dev);

	struct nt726xx_gpio_reg_offs *regs;
};


typedef struct NTGPIO_Context_s {
	int             open;
	int             gpiomode;
	int             inputmode;
	int             default_inputmode;
	int             default_level;
	int             function_mode;
	unsigned int   pin_idx;
	unsigned int   *p_pin_clear;
	unsigned int   *p_pin_set;
	unsigned int   dir_idx;
	unsigned int   *p_dir;
} NTGPIO_Context_t;

struct ST_KER_GPIO_ALT_MUX_MAP GPIO_ALT_MUX_TBL[] =
{
		{0, 0x0000000F},
		{1, 0x000000F0},
		{2, 0x00000F00},
		{3, 0x0000F000},
		{4, 0x000F0000},
		{5, 0x00F00000},
		{6, 0x0F000000},
		{7, 0xF0000000},
};

/*
 *  variables
 */
static struct semaphore		g_gpio_sem;
static struct GPIO_REG_s *gstMIPS_gpio_gp[EN_KER_GPIO_GROUP_MIPS_GROUP_NUM] = {
			NULL};
static GPIO_DS_REG_t *gstgpio_ds_gp[EN_KER_GPIO_GROUP_MIPS_GROUP_NUM] = {NULL};


struct ST_SYS_RM_BLK {
  /* !< physical address, default : 0 */
	unsigned int u32PhysAddr;
	/* !< phy end address = u32PhysBgn + SIZE - 1, default : 0xffffffff */
	unsigned int u32DataSize;
};


struct ST_SYS_RM_MAP {
	int           iID;         /* !< index of register map info */
	unsigned int  u32Status;   /* !< EN_SYS_RM_FLAG */

	struct ST_SYS_RM_BLK stRMBlock;

	unsigned int u32PhysBgn;
	/* !< phy end address = u32PhysBgn + SIZE - 1, default : 0xffffffff */
	unsigned int u32PhysEnd;

	/* !< vir begin address after ioremap, default : 0 */
	unsigned int u32VirtBgn;
	/* !< vir end address = u32VirtBgn + SIZE - 1, default : 0xffffffff */
	unsigned int u32VirtEnd;
};

ST_KER_GPIO_DRIVING_PARAM *GPIO_DRIVING_TBL = NULL;
NTGPIO_PadConfig_t *GPIO_PAD_Table = NULL;

#define SEM_DEINT_CNT 0
#define VA_SYS_REG_MAP_NUM         (16)
static NTGPIO_Context_t g_kgpioContext[EN_KER_GPIO_PIN_TOTAL];
static int g_gpioDriverInit = FALSE;
static int g_gpioDeviceOpen = FALSE;

#define HDMI_PORT_IDENT_MAX (sizeof(ST_KER_GPIO_OP_HDMI_IDENT))
#define MHL_IDENT_VALUE		BIT_24
static void __iomem *pHDMIIdent_vr, *pMHLIdent_vr;

/* GPIO functon is enabled when MUX Bit = 1 */
enum EN_KER_GPIO_PIN gstu8Default_Function_Pin_Table[] = {
	EN_KER_GPIO_PIN_NULL
};
/* GPIO with Interrupt function pin, please list in order */
/* TODO in 14 */
enum EN_KER_GPIO_PIN gstu8Interrupt_Pin_Table[] = {
	EN_KER_GPIO_GPC_6, EN_KER_GPIO_GPC_1,  EN_KER_GPIO_GPC_2,
	EN_KER_GPIO_GPC_3, EN_KER_GPIO_GPC_4, EN_KER_GPIO_GPC_5,
	EN_KER_GPIO_GPC_12, EN_KER_GPIO_GPC_13, EN_KER_GPIO_GPC_14
};

struct ST_GPIO_GROUP_INFO
	gstGPIO_Grp_Info_Tbl[EN_KER_MIPS_GPIO_GROUP_MIPS_MAX] = {
	{EN_KER_GPIO_GPA_NUM, EN_KER_GPIO_GPA_START, EN_KER_GPIO_GPA_END},
	{EN_KER_GPIO_GPB_NUM, EN_KER_GPIO_GPB_START, EN_KER_GPIO_GPB_END},
	{EN_KER_GPIO_GPC_NUM, EN_KER_GPIO_GPC_START, EN_KER_GPIO_GPC_END},
	{EN_KER_GPIO_GPD_NUM, EN_KER_GPIO_GPD_START, EN_KER_GPIO_GPD_END},
	{EN_KER_GPIO_GPE_NUM, EN_KER_GPIO_GPE_START, EN_KER_GPIO_GPE_END},
#if	!defined(CONFIG_ARCH_NVT72172)
	{EN_KER_GPIO_GPH_NUM, EN_KER_GPIO_GPH_START, EN_KER_GPIO_GPH_END},
	{EN_KER_GPIO_GPK_NUM, EN_KER_GPIO_GPK_START, EN_KER_GPIO_GPK_END},
#endif
};

/*
 * prototype
 */
static int ntgpio_OpenGPIO(enum EN_KER_GPIO_PIN pin, bool gpiomode);
static void ntgpio_CloseGPIO(enum EN_KER_GPIO_PIN pin);
static int ntgpio_SetLevel(enum EN_KER_GPIO_PIN pin, bool level);
static int ntgpio_GetLevel(enum EN_KER_GPIO_PIN pin, bool *level);
static int ntgpio_SetDirection(enum EN_KER_GPIO_PIN pin, bool inputmode);
static int  ntgpio_SetAltMode(enum EN_KER_GPIO_PIN pin, unsigned char altmode);
static int  ntgpio_GetAltMode(enum EN_KER_GPIO_PIN pin, unsigned char *altmode);
static int  ntgpio_Open(ST_KER_GPIO_INIT_PARAM *openParam);
static void ntgpio_Close(void);
static int ntgpio_ConfigPadMode(enum EN_KER_GPIO_PIN pin,
	unsigned short u16Mode);
static int  ntgpio_SetDriving( enum EN_KER_GPIO_PIN pin, unsigned char u8Driving );
static int  ntgpio_GetDriving( enum EN_KER_GPIO_PIN pin, unsigned char *u8Driving );
static int ntgpio_ConfigIntType(enum EN_KER_GPIO_PIN pin,
	EN_KER_GPIO_INT_TYPE enIntType);
static int ntgpio_AltModeInit(ST_KER_GPIO_ALT_INIT *pAltIniParm,
	unsigned int ulcount);
static int ntgpio_DriverInit(void);
static void ntgpio_DriverExit(void);

static bool gpio_default_function_pin_search(enum EN_KER_GPIO_PIN enPin);
static bool gpio_group_search(enum EN_KER_GPIO_PIN enPin,
	EN_DRV_MIPS_GPIO_GROUP *penGroup);


/* ========================================================================// */
/*  Utility Func. */
/* ========================================================================// */

/* Low-level register write function */
static inline void gpio_wr_reg(void __iomem *reg, unsigned int val)
{
	writel(val, reg);
}

/* Low-level register read function */
static inline unsigned int gpio_rd_reg(void __iomem *reg)
{
	return readl(reg);
}


/* ========================================================================// */
/*  Goal : Fast boot */
/* ========================================================================// */
static int _ker_gpio_suspend_p(struct device *dev);
static int _ker_gpio_resume_p(struct device *dev);

typedef enum {
	GPIO_CMD_INIT,
	GPIO_CMD_PREINIT,
	GPIO_CMD_FUNCTION,

	GPIO_CMD_OPEN,
	GPIO_CMD_LEVEL,
	GPIO_CMD_DIRECTION,
	GPIO_CMD_ALTMOD,
	GPIO_CMD_PADMODE,
	GPIO_CMD_INTTYPE,
	GPIO_CMD_DRIVING,

	GPIO_CMD_INVALID

} _DRV_GPIO_CMD_TYPE_en;

typedef struct {
	enum EN_KER_GPIO_PIN				enPinId;
	EN_KER_GPIO_LEVEL			enLevel;
	EN_KER_GPIO_DIRECTION		enDirection;
	EN_KER_GPIO_INT_TYPE		enIntType;
	EN_KER_GPIO_ALT_MODE		enAltMode;
	unsigned char					bGpioMode;
	unsigned int				u16PadMode;
	unsigned char				u8Driving;
	ST_KER_GPIO_INIT_PARAM		stInit;
	ST_KER_GPIO_ALT_INIT *pstFunInit;

} _DRV_GPIO_CFG_CELL_t;

struct _DRV_GPIO_REC_t {
	_DRV_GPIO_CMD_TYPE_en		enCmd;
	struct _DRV_GPIO_REC_t *pstNext;
	unsigned long					u32Count;
	char							pcData[1];
};

struct _recovery_item_t {
	unsigned int				u16Pin;
	unsigned char				u8Level;
	struct _recovery_item_t	*pstNext;
};

typedef struct {
	unsigned char	u8KernelMode;
	unsigned char	u8Suspend;
	unsigned char	u8Resmue;
	unsigned char	u8IOTbl[EN_KER_GPIO_PIN_TOTAL+1];

	struct _recovery_item_t *pstRecoveryList;
	struct semaphore			stSema;

	struct _DRV_GPIO_REC_t *pstGpioRec;
	struct _DRV_GPIO_REC_t *pstLastRec;

} _DRV_GPIO_CTRL_t;

static const struct dev_pm_ops _ker_gpio_pm_ops = {
	.resume = _ker_gpio_resume_p,
	.suspend = _ker_gpio_suspend_p,
};

static _DRV_GPIO_CTRL_t g_stGPIOCtrl;


static _DRV_GPIO_CTRL_t *_drv_gpio_load_p(void);
static bool _drv_gpio_chk_rec_p(_DRV_GPIO_CMD_TYPE_en enCmd,
	void *vpParam);
static void _drv_gpio_record_add_p(_DRV_GPIO_CMD_TYPE_en enCmd,
	unsigned char u8User, void *vpParam, unsigned long ulCount);
#define MA_KER_REG_REMAP(reg) ioremap_nocache((unsigned int)(reg), 0x18)

static _DRV_GPIO_CTRL_t *_drv_gpio_load_p(void)
{
	static unsigned char u8Init = FALSE;

	if (!u8Init) {
		memset((char *)&g_stGPIOCtrl, 0x00,
		 sizeof(_DRV_GPIO_CTRL_t));
		sema_init(&g_stGPIOCtrl.stSema, 1);
		u8Init = TRUE;
	}

	return &g_stGPIOCtrl;
}

static bool _drv_gpio_chk_rec_p(_DRV_GPIO_CMD_TYPE_en enCmd, void *vpParam)
{
	unsigned long u32Addr = 0;
	bool bResult = FALSE;
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;
	_DRV_GPIO_CFG_CELL_t *pstGpioCell =
		(_DRV_GPIO_CFG_CELL_t *)vpParam, *pstCurrCell = NULL;
	struct _DRV_GPIO_REC_t *pstCurrItem = NULL;
	enum EN_KER_GPIO_PIN enPid = EN_KER_GPIO_PIN_NULL;

	pstMetaData = _drv_gpio_load_p();
	if (!pstMetaData || !pstGpioCell)
		goto END_CHK;

	enPid = pstGpioCell->enPinId;
	pstCurrItem = pstMetaData->pstGpioRec;

	while (pstCurrItem) {
		u32Addr = (unsigned long)&pstCurrItem->pcData[0];
		pstCurrCell = (_DRV_GPIO_CFG_CELL_t *)u32Addr;

		if (pstCurrItem->enCmd == enCmd) {
			if ((enCmd == GPIO_CMD_INIT || enCmd == GPIO_CMD_PREINIT
				|| enCmd == GPIO_CMD_FUNCTION)) {
				bResult = TRUE;
				break;
			} else if (pstCurrCell->enPinId == enPid) {
				switch (enCmd) {
				case GPIO_CMD_OPEN:
					if (pstCurrCell->bGpioMode
						 != pstGpioCell->bGpioMode)
						pstCurrCell->bGpioMode =
						 pstGpioCell->bGpioMode;

					bResult = TRUE;
					break;
				case GPIO_CMD_LEVEL:
					if (pstCurrCell->enLevel
						!= pstGpioCell->enLevel)
						pstCurrCell->enLevel =
						 pstGpioCell->enLevel;

					bResult = TRUE;
					break;
				case GPIO_CMD_DIRECTION:
					if (pstCurrCell->enDirection
						!= pstGpioCell->enDirection)
						pstCurrCell->enDirection =
						pstGpioCell->enDirection;

					bResult = TRUE;
					break;
				case GPIO_CMD_ALTMOD:
					if (pstCurrCell->enAltMode
						!= pstGpioCell->enAltMode)
						pstCurrCell->enAltMode =
						 pstGpioCell->enAltMode;

					bResult = TRUE;
					break;
				case GPIO_CMD_PADMODE:
					if (pstCurrCell->u16PadMode
						== pstGpioCell->u16PadMode)
							bResult = TRUE;

						break;
				case GPIO_CMD_INTTYPE:
					if (pstCurrCell->enIntType
						== pstGpioCell->enIntType)
							bResult = TRUE;

					break;
				default: {
						bResult = TRUE;
						goto END_CHK;
					}
				}

				break;
			}

		}
		pstCurrItem = pstCurrItem->pstNext;
	}

END_CHK:
	return bResult;
}

static void _drv_gpio_record_add_p(_DRV_GPIO_CMD_TYPE_en enCmd,
	unsigned char u8User, void *vpParam, unsigned long ulCount)
{
	void	*ptr;
	int iParamSize = 0, iExist = FALSE;
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;

	pstMetaData = _drv_gpio_load_p();
	if (!pstMetaData || !vpParam)
		goto END_ADD;



	/* ! Notice : If item exists, just update it. */
	if (_drv_gpio_chk_rec_p(enCmd, vpParam)) {
		iExist = TRUE;
		goto END_ADD;
	}

	iParamSize = sizeof(_DRV_GPIO_CFG_CELL_t);
	if (enCmd == GPIO_CMD_INIT)
		iParamSize = sizeof(ST_KER_GPIO_INIT_PARAM);
	else if (enCmd == GPIO_CMD_FUNCTION)
		iParamSize = sizeof(ST_KER_GPIO_ALT_INIT)*ulCount;
	else if (enCmd == GPIO_CMD_OPEN)
		iParamSize = sizeof(_DRV_GPIO_CFG_CELL_t)*ulCount;
	else if (enCmd == GPIO_CMD_ALTMOD)
		iParamSize = sizeof(_DRV_GPIO_CFG_CELL_t)*ulCount;
	else if (enCmd == GPIO_CMD_DIRECTION)
		iParamSize = sizeof(_DRV_GPIO_CFG_CELL_t)*ulCount;
	else if (enCmd == GPIO_CMD_LEVEL)
		iParamSize = sizeof(_DRV_GPIO_CFG_CELL_t)*ulCount;


	{
		struct _DRV_GPIO_REC_t *pstNewItem = NULL, *pstCurrItem = NULL;

		ptr = kmalloc(sizeof(struct _DRV_GPIO_REC_t) +
			iParamSize, GFP_KERNEL);
		pstNewItem = (struct _DRV_GPIO_REC_t *)ptr;
		if (pstNewItem) {
			if (u8User) {
				if (copy_from_user
				((unsigned char *)&pstNewItem->pcData[0],
				(unsigned char *)vpParam, iParamSize)) {
					kfree(pstNewItem);
					goto END_ADD;
				}
			} else {
				memcpy((__signed char *)&pstNewItem->pcData[0],
					(__signed char *)vpParam, iParamSize);
			}
			pstNewItem->u32Count = ulCount;
			pstNewItem->enCmd = enCmd;
			pstNewItem->pstNext = NULL;
			pstCurrItem = pstMetaData->pstGpioRec;
			if (!pstMetaData->pstGpioRec) {
				pstMetaData->pstGpioRec = pstNewItem;
				pstMetaData->pstLastRec =
					pstMetaData->pstGpioRec;
			} else {
				pstMetaData->pstLastRec->pstNext = pstNewItem;
				pstMetaData->pstLastRec =
					pstMetaData->pstLastRec->pstNext;
			}
		}
	}

END_ADD:
	return;
}

static void ntgpio_Initialise(void)
{
	void	*ptr;
	ST_KER_GPIO_INIT_PARAM  *pstInitParam;
	unsigned int u32Idx;

	ptr =
		kmalloc(sizeof(ST_KER_GPIO_INIT_PARAM), GFP_KERNEL);
	pstInitParam = (ST_KER_GPIO_INIT_PARAM  *)ptr;
	if (pstInitParam == NULL)
		return;

	for (u32Idx = 0; u32Idx < EN_KER_MIPS_GPIO_GROUP_MIPS_MAX; u32Idx++) {

		if (u32Idx == EN_KER_MIPS_GPIO_GROUP_MIPS_GPB) {
			pstInitParam->u32GPIOMode[u32Idx] = 0xffffffff;
			pstInitParam->u32Direction[u32Idx] = 0xffffffff;
			pstInitParam->u32Level[u32Idx] = 0xFFFFF3CF;
			pstInitParam->u32ActiveMode[u32Idx] = 0xFFFFFFFF;
		} else {
			pstInitParam->u32GPIOMode[u32Idx] = 0xffffffff;
			pstInitParam->u32Direction[u32Idx] = 0xffffffff;
			pstInitParam->u32Level[u32Idx] = 0xFFFFFFFF;
			pstInitParam->u32ActiveMode[u32Idx] = 0xFFFFFFFF;
		}


	}

	if (ntgpio_Open(pstInitParam) == FALSE) {
			kfree(pstInitParam);
			return;
		}
	kfree(pstInitParam);
}

static int _ker_gpio_suspend_p(struct device *dev)
{
	/* ! Notice : It doesn't need to do. */
	GPIO_DBG_LOG("=================\n");
	return 0;
}

static int _ker_gpio_resume_p(struct device *dev)
{
	static unsigned char u8Resume_Init = FALSE;
	bool bResult = FALSE;
	unsigned long u32Addr = 0;
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;
	struct _DRV_GPIO_REC_t *pstCurrItem = NULL;
	struct _recovery_item_t *pstCurr = NULL, *pstDel = NULL;
	_DRV_GPIO_CFG_CELL_t *pstGpioCell =
		(_DRV_GPIO_CFG_CELL_t *)NULL;

	GPIO_DBG_LOG("###  start\n");

	pstMetaData = _drv_gpio_load_p();
	if (!pstMetaData) {
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR" pstMetaData is empty!!\n");
		goto END_RESUME;
	}

	pstCurrItem = pstMetaData->pstGpioRec;
	pstMetaData->u8Suspend = FALSE;

	pstMetaData->u8Resmue = TRUE;
	while (pstCurrItem) {
		u32Addr = (unsigned long)&pstCurrItem->pcData[0];
		pstGpioCell = (_DRV_GPIO_CFG_CELL_t *)u32Addr;

		if (!u8Resume_Init) {
			ntgpio_DriverInit();
			if (ntgpio_Open(
				(ST_KER_GPIO_INIT_PARAM *)&u32Addr)
				 == FALSE) {
				GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
				" ntgpio_Open is failed after resuming!!\n");
				goto END_RESUME;
			}
			GPIO_DBG_LOG("u8Resume_Init be accumulated once.\r\n");
			u8Resume_Init = TRUE;
		}
#if defined(CONFIG_ARCH_NVT72671D)
		if(pstGpioCell->enPinId == AMP_RESET_PIN)
		{
			if(pstCurrItem->enCmd == GPIO_CMD_LEVEL ||
				pstCurrItem->enCmd == GPIO_CMD_DIRECTION) {
				pstCurrItem = pstCurrItem->pstNext;
				continue;
			}
		}
#endif
		switch (pstCurrItem->enCmd) {
		case GPIO_CMD_INIT:
			ntgpio_DriverInit();
			bResult =
			ntgpio_Open((ST_KER_GPIO_INIT_PARAM *)&u32Addr);
			break;
		case GPIO_CMD_PREINIT:
			bResult =
			ntgpio_Open((ST_KER_GPIO_INIT_PARAM *)&u32Addr);
			break;
		case GPIO_CMD_FUNCTION:
			bResult =
			ntgpio_AltModeInit(
			(ST_KER_GPIO_ALT_INIT *)u32Addr,
			(unsigned int)pstCurrItem->u32Count);
			break;
		case GPIO_CMD_OPEN:
			bResult =
			ntgpio_OpenGPIO(pstGpioCell->enPinId,
			 pstGpioCell->bGpioMode);
			break;
		case GPIO_CMD_LEVEL:
			{
			switch (pstGpioCell->enLevel) {
			case 0:
					bResult =
					 ntgpio_SetLevel(pstGpioCell->enPinId
					 , FALSE);
					break;
			case 1:
					bResult =
					 ntgpio_SetLevel(pstGpioCell->enPinId,
					  TRUE);
					break;
			default:
					{
						break;
					}
				}
				break;
			}
		case GPIO_CMD_DIRECTION:
			{
			switch (pstGpioCell->enDirection) {
			case 0:
					bResult =
					ntgpio_SetDirection(
					pstGpioCell->enPinId, FALSE);
					break;
			case 1:
					bResult =
					ntgpio_SetDirection(
					pstGpioCell->enPinId, TRUE);
					break;
			default:
					{
						break;
					}
				}

			break;
			}
		case GPIO_CMD_ALTMOD:
			bResult =
				ntgpio_SetAltMode(pstGpioCell->enPinId,
				 pstGpioCell->enAltMode);
			break;
		case GPIO_CMD_PADMODE:
			bResult =
				ntgpio_ConfigPadMode(pstGpioCell->enPinId,
				 pstGpioCell->u16PadMode);
			break;
		case GPIO_CMD_INTTYPE:
			bResult =
				ntgpio_ConfigIntType(pstGpioCell->enPinId,
				 pstGpioCell->enIntType);
			break;
		case GPIO_CMD_INVALID:
		default:
			{
				break;
			}
		}

		pstCurrItem = pstCurrItem->pstNext;
	}

	down(&pstMetaData->stSema);
	pstCurr = pstMetaData->pstRecoveryList;
	while (pstCurr) {
		pstDel = pstCurr;
		if (ntgpio_SetLevel((unsigned char)pstCurr->u16Pin,
			((pstCurr->u8Level) == 0 ? 0 : 1)) == FALSE)
			GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
			"ntgpio_SetLevel error!!\n");

		pstCurr = pstCurr->pstNext;

		pstDel->pstNext = NULL;
		kfree(pstDel);
	}

	pstMetaData->pstRecoveryList = NULL;
	up(&pstMetaData->stSema);

	pstMetaData->u8Resmue = FALSE;

	GPIO_DBG_LOG(" end\n");
	bResult = TRUE;
	return 0;
END_RESUME:
	GPIO_DBG_LOG("does not work\n");
	return -1;
}

/* ========================================================================// */
/*
 * General interface
 */

int KER_GPIO_DrvInit(void)
{
	unsigned char i = 0, j = 0;
	sema_init(&g_gpio_sem, 1);

	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPA] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPA);
	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPB] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPB);
	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPC] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPC);
	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPD] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPD);
	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPE] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPE);
#if	!defined(CONFIG_ARCH_NVT72172)
	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPH] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPH);

	gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPK] =
		(struct GPIO_REG_s *)
		MA_KER_REG_REMAP(KER_GPIO_GROUP_PHY_ADDR_GPK);
#endif


	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPA][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPA]);
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPB][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPB]);
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPC][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPC]);
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPD][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPD]);
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPE][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPE]);
#if	!defined(CONFIG_ARCH_NVT72172)
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPH][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPH]);
	GPIO_DBG_LOG(
		"gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPK][%p]\n\r",
		gstMIPS_gpio_gp[EN_KER_MIPS_GPIO_GROUP_MIPS_GPK]);
#endif
#if	defined(CONFIG_ARCH_NVT72172)
	GPIO_DBG_LOG("Set table as 172\n");
	GPIO_DRIVING_TBL =	GPIO_DRIVING_TBL_172;
	GPIO_PAD_Table = GPIO_PAD_Table_172;
#elif defined(CONFIG_ARCH_NVT72673)
	GPIO_DBG_LOG("Set table as 673\n");
	GPIO_DRIVING_TBL =	GPIO_DRIVING_TBL_673;
	GPIO_PAD_Table = GPIO_PAD_Table_673;
#elif defined(CONFIG_ARCH_NVT72671D)
	GPIO_DBG_LOG("Set table as 671d\n");
	GPIO_DRIVING_TBL =	GPIO_DRIVING_TBL_671D;
	GPIO_PAD_Table = GPIO_PAD_Table_671D;		
#else
	GPIO_DBG_LOG("unknown chip type\n");
	return -EINVAL;
#endif

	for(i = 0; i <= (unsigned char)EN_KER_MIPS_GPIO_GROUP_MIPS_END; i++)
	{
		for(j = 0; j <= (unsigned char)EN_KER_MIPS_GPIO_GROUP_MIPS_END; j++)
		{
			if(GPIO_DRIVING_TBL[j].enGrp == (EN_DRV_MIPS_GPIO_GROUP)i)
			{
				if(GPIO_DRIVING_TBL[j].u32PhyAddr == 0)
					gstgpio_ds_gp[j] =	NULL;
				else
					gstgpio_ds_gp[j] =	(pGPIO_DS_REG_t)MA_KER_REG_REMAP(GPIO_DRIVING_TBL[i].u32PhyAddr);
				break;
			}		
		}
	}

	/** for HDMI ident **/
	pHDMIIdent_vr = ioremap_nocache(KER_GPIO_HDMI_IDENT_PHY_ADDR, 1);
	pMHLIdent_vr = ioremap_nocache(KER_GPIO_MHL_IDENT_PHY_ADDR, 1);


	ntgpio_DriverInit();
	GPIO_DBG_LOG("Novatek GPIO Driver registered\n");

	return TRUE;
}


void KER_GPIO_DrvExit(void)
{
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;

	pstMetaData = _drv_gpio_load_p();
	if (!pstMetaData)
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR" g_stGPIOCtrl is NULL!!\n");

	if (down_trylock(&g_gpio_sem) != 0)
		up(&g_gpio_sem);

	sema_init(&g_gpio_sem, SEM_DEINT_CNT);
	sema_init(&pstMetaData->stSema, SEM_DEINT_CNT);

	ntgpio_DriverExit();
	GPIO_DBG_LOG("Novatek GPIO Driver unloaded\n");

}


/*
 * General interface convenience methods
 */

/* ************************************************************************** */

/*  Function Name: ntgpio_OpenGPIO */

/*  Description:   open gpio pin */

/*  Parameters:    pin - pin id */
/*                 gpiomode - TRUE : gpio mode */
/*                            FALSE : alternative mode */

/*  Returns:       TRUE if successful */
/*                 FALSE if fail */

/* ************************************************************************** */
static int  ntgpio_OpenGPIO(enum EN_KER_GPIO_PIN pin, bool gpiomode)
{

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		/*   return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		return FALSE;
	}

	if (g_kgpioContext[pin].open == TRUE) {
		/*  pin already open */
		/* return FALSE; */
	}

	g_kgpioContext[pin].open = TRUE;

	#if 0
	if (gpiomode == TRUE) {

		if (TRUE == gpio_default_function_pin_search(pin)) {
			/*** These pins are GPIOs as mux bit is set to 1. ***/{
			if (ntgpio_SetAltMode(pin, 1) == FALSE)
				GPIO_INF_LOG("SetAltMd error, pin = %d\n",
					pin);
			}
		} else {

			if (ntgpio_SetAltMode(pin, !gpiomode) == FALSE)
				GPIO_INF_LOG("SetAltMd error, pin = %d\n",
					pin);
		}
	} else {

	}
	#endif
	return TRUE;
}

/* ************************************************************************** */

/*  Function Name: ntgpio_CloseGPIO */

/*  Description:   close gpio pin */

/*  Parameters:    pin - pin id */

/*  Returns:       none */

/* ************************************************************************** */
static void ntgpio_CloseGPIO(enum EN_KER_GPIO_PIN pin)
{
	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		/*   return; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		return;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		return;
	}

	g_kgpioContext[pin].open = FALSE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_SetLevel */
/*  Description:   set pin level */
/*  Parameters:    pin - pin id */
/*                 level - pin level */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_SetLevel(enum EN_KER_GPIO_PIN pin, bool level)
{
	unsigned int   v;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		GPIO_INF_LOG(" UIO_GPIO device doesn't open\n");
		/*  return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] invalid pin\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not yet open\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].gpiomode == FALSE) {
		/*  pin not gpio mode */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not gpio mode\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].inputmode == TRUE) {
		/*  not output mode */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not output\n", pin);
		return FALSE;
	}

	if (level == TRUE) {
		v = *(g_kgpioContext[pin].p_pin_set);
		v = v | (1L << g_kgpioContext[pin].pin_idx);
		*(g_kgpioContext[pin].p_pin_set) = v;
	} else {
		*(g_kgpioContext[pin].p_pin_clear) =
				(1L << g_kgpioContext[pin].pin_idx);
	}
	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_GetLevel */
/*  Description:   get pin level */
/*  Parameters:    pin - pin id */
/*                 *level - return pin level */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_GetLevel(enum EN_KER_GPIO_PIN pin, bool *level)
{
	unsigned int   v;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		GPIO_INF_LOG(" UIO_GPIO device doesn't open\n");
		/* return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL || level == NULL) {
		/*  invalid pin or parameter */
		GPIO_INF_LOG(" UIO_GPIO invalid pin[%d]\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not yet open\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].gpiomode == FALSE) {
		/*  pin not gpio mode */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not gpio mode\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].inputmode == FALSE) {
		/*  output mode */
		down(&g_gpio_sem);
		v = *(g_kgpioContext[pin].p_pin_set);
		v = v & (1L << g_kgpioContext[pin].pin_idx);
		up(&g_gpio_sem);

		if (v)
			*level = TRUE;
		else
			*level = FALSE;

	} else {
		/* input mode */
		down(&g_gpio_sem);
		v = *(g_kgpioContext[pin].p_pin_clear);
		v = v & (1L << g_kgpioContext[pin].pin_idx);
		up(&g_gpio_sem);

		if (v)
			*level = TRUE;
		else
			*level = FALSE;

	}

	return TRUE;
}



/* ************************************************************************** */
/*  Function Name: ntgpio_SetDirection */
/*  Description:   set pin direction */
/*  Parameters:    pin - pin id */
/*                 inputmode - TRUE if input mode */
/*                             FALSE if output mode */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_SetDirection(enum EN_KER_GPIO_PIN pin, bool inputmode)
{
	unsigned int v;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		GPIO_INF_LOG(" UIO_GPIO device doesn't open\n");
		/*  return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		GPIO_INF_LOG(" UIO_GPIO invalid pin[%d]\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not yet open\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].gpiomode == FALSE) {
		/*  pin not gpio mode */
		GPIO_DBG_LOG(" UIO_GPIO pin[%d] not gpio mode\n", pin);
		g_kgpioContext[pin].gpiomode = TRUE;
		GPIO_DBG_LOG(" UIO_GPIO pin[%d] set as gpio mode\n", pin);
		/* return FALSE; */
	}


	if (inputmode == TRUE) {
		/*  set high before change to input mode */
		g_kgpioContext[pin].inputmode = TRUE;

		v = *(g_kgpioContext[pin].p_dir);
		v = v & ~(1L << g_kgpioContext[pin].dir_idx);
		*(g_kgpioContext[pin].p_dir) = v;
	} else {
		g_kgpioContext[pin].inputmode = FALSE;

		v = *(g_kgpioContext[pin].p_dir);
		v = v | (1L << g_kgpioContext[pin].dir_idx);
		*(g_kgpioContext[pin].p_dir) = v;
	}

	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_GetDirection */
/*  Description:   get pin direction */
/*  Parameters:    pin - pin id */
/*                 *inputmode - return pin direction */
/*                              TRUE if input mode */
/*                              FALSE if output mode */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */


/* ************************************************************************** */
/*  Function Name: ntgpio_SetAltMode */
/*  Description:   set pin mode */
/*  Parameters:    pin - pin id */
/*                 altmode - 0 if gpio mode */
/*                           1~ 3 if alternative mode */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_SetAltMode(enum EN_KER_GPIO_PIN pin, unsigned char altmode)
{
	unsigned int u32mux_reg;
	EN_DRV_MIPS_GPIO_GROUP enGroup;
	unsigned int *pu32table_pin_addr = NULL;
	unsigned char u8mux_reg_index = 0, funGrp_mask = 0;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		GPIO_INF_LOG(" UIO_GPIO device doesn't open\n");
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		GPIO_INF_LOG(" UIO_GPIO: invalid pin[%d] or parameter\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		GPIO_INF_LOG(" UIO_GPIO: pin[%d] not yet open\n", pin);
		return FALSE;
	}

	if (TRUE == gpio_group_search(pin, &enGroup)) {
		if (pin <= EN_KER_GPIO_GPA_END) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPA_START)>>3;
			GPIO_DBG_LOG("Set GrpA pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		} else if ((pin <= EN_KER_GPIO_GPB_END) &&
					(pin >= EN_KER_GPIO_GPB_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPB_START)>>3;
			GPIO_DBG_LOG("Set GrpB pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		} else if ((pin <= EN_KER_GPIO_GPC_END) &&
					(pin >= EN_KER_GPIO_GPC_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPC_START)>>3;
			GPIO_DBG_LOG("Set GrpC pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		} else if ((pin <= EN_KER_GPIO_GPD_END) &&
					(pin >= EN_KER_GPIO_GPD_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPD_START)>>3;
			GPIO_DBG_LOG("Set GrpD pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		} else if ((pin <= EN_KER_GPIO_GPE_END) &&
					(pin >= EN_KER_GPIO_GPE_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPE_START)>>3;
			GPIO_DBG_LOG("Set GrpE pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		}
#if	!defined(CONFIG_ARCH_NVT72172)
		else if ((pin <= EN_KER_GPIO_GPH_END) &&
					(pin >= EN_KER_GPIO_GPH_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPH_START)>>3;
			GPIO_DBG_LOG("Set GrpH pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		}
		else if ((pin <= EN_KER_GPIO_GPK_END) &&
					(pin >= EN_KER_GPIO_GPK_START)) {
			u8mux_reg_index = (pin - EN_KER_GPIO_GPK_START)>>3;
			GPIO_DBG_LOG("Set GrpK pin[%d] as muxRegIdx[%d]\n",
				pin, u8mux_reg_index);
		}
#endif		
		else {
			GPIO_INF_LOG("OutOfRange, pin[%d], muxRegIdx[%d]\n",
					pin, u8mux_reg_index);
		}


		u32mux_reg =
			gstMIPS_gpio_gp[enGroup]->ulGPIOMUX[u8mux_reg_index];

		funGrp_mask = (pin - 
			gstGPIO_Grp_Info_Tbl[enGroup].u8group_start) % 8;

		if (altmode >= 1) {
			u32mux_reg &= ~(GPIO_ALT_MUX_TBL[funGrp_mask].u32AltMux);
			u32mux_reg |= altmode << 
				GPIO_ALT_MUX_TBL[funGrp_mask].u8GrpOffset * 4;
		} else {
			g_kgpioContext[pin].gpiomode = TRUE;
			u32mux_reg &= ~(GPIO_ALT_MUX_TBL[funGrp_mask].u32AltMux);
		}

		gstMIPS_gpio_gp[enGroup]->ulGPIOMUX[u8mux_reg_index] =
			 u32mux_reg;
	}

	if (altmode >= 1)
		g_kgpioContext[pin].function_mode = TRUE;
	else
		g_kgpioContext[pin].function_mode = FALSE;

	return TRUE;
}



/* ************************************************************************** */
/*  Function Name: ntgpio_GetAltMode */
/*  Description:   get pin mode */
/*  Parameters:    pin - pin id */
/*                 *altmode - return gpio mode */
/*                            0 if gpio mode */
/*                            1~3 if alternative mode */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_GetAltMode(enum EN_KER_GPIO_PIN pin, unsigned char *altmode)
{
	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		GPIO_INF_LOG(" UIO_GPIO device doesn't open\n");
		/* return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin or parameter */
		GPIO_INF_LOG(" UIO_GPIO invalid pin[%d] or parameter\n", pin);
		return FALSE;
	}

	if (g_kgpioContext[pin].open == FALSE) {
		/*  pin not yet open */
		GPIO_INF_LOG(" UIO_GPIO pin[%d] not yet open\n", pin);
		return FALSE;
	}

	*altmode = g_kgpioContext[pin].function_mode;

	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_Open */
/*  Description:   open gpio */
/*  Parameter:    *openParam - gpio parameter */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_Open(ST_KER_GPIO_INIT_PARAM *openParam)
{
	unsigned int u32Par_GPIOMode;
	unsigned int u32Par_Dir;
	unsigned int u32Par_Level;
	unsigned int u32Par_ACTMode;

	unsigned int u32Reg_Dir;
	unsigned int u32Reg_Mux;
	unsigned int u32Reg_Level_Set;
	unsigned int u32Reg_Level_Clear;

	unsigned char   i, j, k, u8mux_reg_index;
	int             value;

	unsigned int  u32INT_status;

	unsigned int *intEnableReg		= NULL;
	unsigned int *intLvlReg	= NULL;
	unsigned int *intStatusReg	= NULL;


	if (g_gpioDriverInit == FALSE) {
		/*  driver not yet initial */
		return FALSE;
	}

	if (openParam == NULL) {
		/*  invalid parameter */
		return FALSE;
	}

	if (g_gpioDeviceOpen == TRUE) {
		/*  device already open */
		/* return TRUE; */
	}
	for (k = 0; k < EN_KER_MIPS_GPIO_GROUP_MIPS_MAX; k++) {
		/* get the GPIO Group register value */
		u32Par_GPIOMode = openParam->u32GPIOMode[k];

		u32Par_Dir = openParam->u32Direction[k];
		u32Reg_Dir = gstMIPS_gpio_gp[k]->ulGPIODir;
		u32Par_Level = openParam->u32Level[k];
		u32Reg_Level_Set = gstMIPS_gpio_gp[k]->ulGPIOSet;
		u32Reg_Level_Clear = 0;
		u32Par_ACTMode = openParam->u32ActiveMode[k];

		for (i = gstGPIO_Grp_Info_Tbl[k].u8group_start, j = 0;
			i <= gstGPIO_Grp_Info_Tbl[k].u8group_end;
			i++, j++) {
			u8mux_reg_index = (i>>3);
			u32Reg_Mux =
				gstMIPS_gpio_gp[k]->ulGPIOMUX[u8mux_reg_index];

			if (!(u32Par_ACTMode & BIT_0)) {
				GPIO_DBG_LOG(" GROOUP_%d SKIP %i\n\r", k, i);
				u32Par_GPIOMode		= u32Par_GPIOMode>>1;
				u32Par_Dir			= u32Par_Dir>>1;
				u32Reg_Level_Set	= u32Reg_Level_Set>>1;
				u32Par_ACTMode = u32Par_ACTMode>>1;
				continue;
			}
			u32Par_ACTMode = u32Par_ACTMode>>1;

			/*  initial mode */
			if (u32Par_GPIOMode & BIT_0)
				value = TRUE;
			else
				value = FALSE;

			g_kgpioContext[i].gpiomode = value;

			if (value == TRUE)
				gpio_IOMuxSel(i, &u32Reg_Mux);


			u32Par_GPIOMode = u32Par_GPIOMode>>1;

			/*  initial level */
			if (u32Par_Level & BIT_0)
				value = TRUE;
			else
				value = FALSE;

			g_kgpioContext[i].default_level = value;

			if (value == TRUE)
				u32Reg_Level_Set = u32Reg_Level_Set
						| (1L<<j);/* set */
			else
				u32Reg_Level_Clear = u32Reg_Level_Clear|
					(1L<<j);  /* clear */


			u32Par_Level = u32Par_Level>>1;

			/*  initial direction */
			if (u32Par_Dir & BIT_0)
				value = TRUE;
			else
				value = FALSE;

			g_kgpioContext[i].inputmode = value;
			g_kgpioContext[i].default_inputmode = value;

			if (value == FALSE)
				u32Reg_Dir = u32Reg_Dir | (1L<<j);


			u32Par_Dir = u32Par_Dir>>1;

		}

		down(&g_gpio_sem);

/*  Alf 20130125 for power drop of USB POWER PIN */
		gstMIPS_gpio_gp[k]->ulGPIODir		= u32Reg_Dir;
		gstMIPS_gpio_gp[k]->ulGPIOSet		= u32Reg_Level_Set;
		gstMIPS_gpio_gp[k]->ulGPIOClear	= u32Reg_Level_Clear;
		up(&g_gpio_sem);
	}

	/*  initial interrupt */
	intEnableReg = (unsigned int *)ioremap_nocache(GPIO_INT_EN_REG, 1);
	intLvlReg = (unsigned int *)ioremap_nocache(GPIO_INT_LV_REG, 1);
	intStatusReg = (unsigned int *)ioremap_nocache(GPIO_INT_ST_REG, 1);

	down(&g_gpio_sem);
	*intEnableReg	= 0;
	*intLvlReg		= 0;
	/*  write 1 to clear */
	u32INT_status	= *intStatusReg;
	*intStatusReg	= u32INT_status;
	up(&g_gpio_sem);

	GPIO_DBG_LOG("+++\n");
	/* AUD_Preinit(); */
	GPIO_DBG_LOG("---\n");


	GPIO_DBG_LOG("NTGPIO open done!\n");
	g_gpioDeviceOpen = TRUE;
	return TRUE;
}


/* ************************************************************************** */
/*  Function Name: ntgpio_Close */
/*  Description:   close gpio */
/*  Parameters:    none */
/*  Returns:       none */
/* ************************************************************************** */
static void ntgpio_Close(void)
{
	unsigned char   i;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		/* return; */
	}

	for (i = 0; i < EN_KER_GPIO_PIN_TOTAL; i++) {
		if (g_kgpioContext[i].open == TRUE)
			ntgpio_CloseGPIO(i);
	}

	g_gpioDeviceOpen = FALSE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_DriverInit */
/*  Description:   initial driver */
/*  Parameters:    none */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_DriverInit(void)
{
	unsigned char   i;
	EN_DRV_MIPS_GPIO_GROUP enGroup;

	if (g_gpioDriverInit == TRUE) {
		/*  driver already initial */
		return TRUE;
	}

	for (i = 0; i < EN_KER_GPIO_PIN_TOTAL; i++) {
		g_kgpioContext[i].open			= FALSE;
		g_kgpioContext[i].inputmode		= FALSE;
		g_kgpioContext[i].default_inputmode	= FALSE;
		g_kgpioContext[i].default_level		= FALSE;
		g_kgpioContext[i].gpiomode		= FALSE;
		g_kgpioContext[i].function_mode		= 0;

		if (TRUE == gpio_group_search(i, &enGroup)) {
			g_kgpioContext[i].pin_idx =
			i - gstGPIO_Grp_Info_Tbl[enGroup].u8group_start;

			g_kgpioContext[i].p_pin_clear = (unsigned int *)
			(&gstMIPS_gpio_gp[enGroup]->ulGPIOClear);

			g_kgpioContext[i].p_pin_set = (unsigned int *)
			(&gstMIPS_gpio_gp[enGroup]->ulGPIOSet);

			g_kgpioContext[i].dir_idx =
			i - gstGPIO_Grp_Info_Tbl[enGroup].u8group_start;

			g_kgpioContext[i].p_dir =
			(unsigned int *)(&gstMIPS_gpio_gp[enGroup]->ulGPIODir);

			GPIO_PAD_Table[i].PullDnReg =
			(unsigned int)(&gstMIPS_gpio_gp[enGroup]->ulGPIOPD);

			GPIO_PAD_Table[i].PullUpReg =
			(unsigned int)(&gstMIPS_gpio_gp[enGroup]->ulGPIOPU);
		}

	}

	g_gpioDriverInit = TRUE;

	ntgpio_Initialise();

	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_DriverExit */
/*  Description:   initial driver */
/*  Parameters:    none */
/*  Returns:       none */
/* ************************************************************************** */
static void ntgpio_DriverExit(void)
{
	if (g_gpioDriverInit == FALSE) {
		/*  driver not yet initial */
		return;
	}

	if (g_gpioDeviceOpen == TRUE)
		ntgpio_Close();


	g_gpioDriverInit = FALSE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_SetDriving */
/*  Description:   set Driving */
/*  Parameters:    pin - pin id */
/*                 u8Driving - driving strength */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int ntgpio_SetDriving( enum EN_KER_GPIO_PIN pin, unsigned char u8Driving )
{
	unsigned int u32DS_1 = 0, u32DS_0 = 0;
	unsigned char shift = 0;
	EN_DRV_MIPS_GPIO_GROUP enGroup;
	unsigned int v;

	if(pin >= EN_KER_GPIO_PIN_TOTAL)
	{
		/* Invalid Pin*/
		return FALSE;
	}
	
	if(g_kgpioContext[pin].open==FALSE)
	{
		// pin not yet open
		GPIO_DBG_LOG("Line(%d) pin(%d) not yet open\n", __LINE__, pin);
		return FALSE;
	}

	if( u8Driving == DRIVING_BYPASS)
	{
		GPIO_DBG_LOG("Bypass, pin = %d\n", pin);
	}
	else
	{
		gpio_group_search(pin, &enGroup);
		u32DS_1 = GPIO_PAD_Table[pin].DS1;
		u32DS_0 = GPIO_PAD_Table[pin].DS0;
		shift = GPIO_PAD_Table[pin].Shift1;
		
		if( u8Driving == (unsigned char)EN_KER_GPIO_PAD_671_CFG_DRIVING_4_0mA  
			|| u8Driving == (unsigned char)EN_KER_GPIO_PAD_673_CFG_DRIVING_3_7mA)
		{
			if(u32DS_1 == 0 || u32DS_0 == 0)
			{
				GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
					"[4_0mA_3_7mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
				return FALSE;			
			}
			GPIO_DBG_LOG("Pin(%d), Set Driving as 4_0mA or 3_7mA\n", pin);
			down(&g_gpio_sem);
			/* Reg_DS1[pin] = 1*/
			v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
			v |= (1L << shift);
			gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

			/* Reg_DS0[pin] = 1*/
			v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
			v |= (1L << shift);
			gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;
			
			up(&g_gpio_sem);
		}
		else if(u8Driving == (unsigned char)EN_KER_GPIO_PAD_671_CFG_DRIVING_3_0mA 
			|| u8Driving == (unsigned char)EN_KER_GPIO_PAD_673_CFG_DRIVING_3_1mA)
		{

			if(u32DS_1 == 0 || u32DS_0 == 0)
			{
				GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
					"[3_0mA_3_1mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
				return FALSE;			
			}
			GPIO_DBG_LOG("Pin(%d), Set Driving as 3_0mA or 3_1mA\n", pin);
			down(&g_gpio_sem);
			/* Reg_DS1[pin] = 1*/
			v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
			v |= (1L << shift);
			gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

			/* Reg_DS0[pin] = 0*/
			v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
			v &= ~(1L << shift);
			gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;
			
			up(&g_gpio_sem);		
		}
		else if(u8Driving == (unsigned char)EN_KER_GPIO_PAD_671_CFG_DRIVING_2_5mA 
			|| u8Driving == (unsigned char)EN_KER_GPIO_PAD_673_CFG_DRIVING_2_5mA)/* maybe 8mA */
		{
			#if defined(CONFIG_ARCH_NVT72671D)
			{
				if(u32DS_1 == 0)/* 8mA */
				{
					if(u32DS_0 == 0)
					{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[8mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
						return FALSE;			
					}
					GPIO_DBG_LOG("Pin(%d), 8mA\n", pin);
					down(&g_gpio_sem);
					/* Reg_DS0[pin] = 1*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
					v |= (1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;					
				}
				else /* 2.5mA */
				{
					if(u32DS_1 == 0 || u32DS_0 == 0)
					{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[2_5mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
						return FALSE;			
					}
					GPIO_DBG_LOG("Pin(%d), 2.5mA\n", pin);
					down(&g_gpio_sem);
					/* Reg_DS1[pin] = 0*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
					v &= ~(1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

					/* Reg_DS0[pin] = 1*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
					v |= (1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;			
				}
			}
			#elif defined(CONFIG_ARCH_NVT72673)
			{
				if(u32DS_1 == 0 || u32DS_0 == 0)
				{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[2_5mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
						return FALSE;				
				}
				GPIO_DBG_LOG("Pin(%d), 2.5mA\n", pin);
				down(&g_gpio_sem);
				/* Reg_DS1[pin] = 0*/
				v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
				v &= ~(1L << shift);
				gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

				/* Reg_DS0[pin] = 1*/
				v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
				v |= (1L << shift);
				gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;
			}
			#endif
			up(&g_gpio_sem);
		}
		else if(u8Driving  == (unsigned char)EN_KER_GPIO_PAD_671_CFG_DRIVING_2_0mA 
			|| u8Driving  == (unsigned char)EN_KER_GPIO_PAD_673_CFG_DRIVING_1_8mA)/* maybe 4mA */
		{
			#if defined(CONFIG_ARCH_NVT72671D)
			{
				if(u32DS_1 == 0)/*4mA*/
				{
					if(u32DS_0 == 0)
					{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[4mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);					
						return FALSE;			
					}
					GPIO_DBG_LOG("Pin(%d), 4mA\n", pin);
					down(&g_gpio_sem);
					/* Reg_DS0[pin] = 0*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
					v &= ~(1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;				
				}
				else /* 2mA *//* u32DS_1 != 0 */
				{
					if(u32DS_0 == 0)
					{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[2_0mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
						return FALSE;			
					}
					GPIO_DBG_LOG("Pin(%d), 2mA\n", pin);
					down(&g_gpio_sem);
					/* Reg_DS1[pin] = 0*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
					v &= ~(1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

					/* Reg_DS0[pin] = 0*/
					v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
					v &= ~(1L << shift);
					gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v;					
				}
			}
			#elif defined(CONFIG_ARCH_NVT72673)
			{
				if(u32DS_1 == 0 || u32DS_0 == 0)
				{
						GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
							"[1_8mA]Pin(%d), Drive(0x%02x), SetDriveErr\n", pin, u8Driving);
						return FALSE;				
				}

				down(&g_gpio_sem);
				/* Reg_DS1[pin] = 0*/
				v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
				v &= ~(1L << shift);
				gstgpio_ds_gp[enGroup]->ulGPIO_DS1 = v;

				/* Reg_DS0[pin] = 0*/
				v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
				v &= ~(1L << shift);
				gstgpio_ds_gp[enGroup]->ulGPIO_DS0 = v; 				
			}
			#endif
			up(&g_gpio_sem);			
		}
		else
			GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
				"Pin(%d), Drive(0x%02x) drive para err\n", pin, u8Driving);
	}
	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_GetDriving */
/*  Description:   Get Driving */
/*  Parameters:    pin - pin id */
/*                 u8Driving - get driving strength */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_GetDriving( enum EN_KER_GPIO_PIN pin, unsigned char *u8Driving )
{
	unsigned int u32DS_1 = 0, u32DS_0 = 0;
	unsigned char shift = 0, u8DS1 = 0, u8DS0 = 0;
	EN_DRV_MIPS_GPIO_GROUP enGroup;
	unsigned int v;

	if(pin >= EN_KER_GPIO_PIN_TOTAL)
	{
		/* Invalid Pin*/
		return FALSE;
	}
	
	if(g_kgpioContext[pin].open == FALSE)
	{
		// pin not yet open
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR"Line(%d) pin(%d) not yet open\n", __LINE__, pin);
		return FALSE;
	}

	{
		
		gpio_group_search(pin, &enGroup);
		u32DS_1 = GPIO_PAD_Table[pin].DS1;
		u32DS_0 = GPIO_PAD_Table[pin].DS0;
		shift = GPIO_PAD_Table[pin].Shift1;
		down(&g_gpio_sem);
		if(u32DS_1 != 0)
	    {
	      //u32DS_0 must != 0
		v = gstgpio_ds_gp[enGroup]->ulGPIO_DS1;
	      u8DS1 = (v & (1L << shift))? 1: 0;
      
		v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
	      u8DS0 = (v & (1L << shift))? 1: 0;
		*u8Driving = (u8DS1 << 1) | u8DS0;
	    }
	    else //u32DS_1 == 0
	    {
	      if(u32DS_0 != 0)
	      {
	        v = gstgpio_ds_gp[enGroup]->ulGPIO_DS0;
	        u8DS0 = (v & (1L << shift))? 1: 0;
	        *u8Driving = u8DS0;
	      }
	      else
	        GPIO_DBG_LOG(" Not support get driving\n"); 
      
	    }
		GPIO_DBG_LOG("pin = %d, u8Driving = %d\n", pin, (int)*u8Driving);
		up(&g_gpio_sem);
		
	}
	return TRUE;
}


/* ************************************************************************** */
/*  Function Name: ntgpio_ConfigPadMode */
/*  Description:   config pad mode */
/*  Parameters:    pin - pin id */
/*                 u16Mode - pad mode */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_ConfigPadMode(enum EN_KER_GPIO_PIN pin,
	unsigned short u16Mode)
{
	unsigned int u32PullUpReg, u32PullUpShift;
	unsigned int u32PullDownReg, u32PullDownShift;
	unsigned int v/*,x*/;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		/* return FALSE; */
	}

	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		return FALSE;
	}

	if (u16Mode & EN_KER_GPIO_PAD_CFG_DEFAULT_SETTING)
		GPIO_DBG_LOG("EN_KER_GPIO_PAD_CFG_DEFAULT_SETTING Mode\n");
	else {
		u32PullUpReg = GPIO_PAD_Table[pin].PullUpReg;
		u32PullUpShift = GPIO_PAD_Table[pin].Shift1;
		u32PullDownReg = GPIO_PAD_Table[pin].PullDnReg;
		u32PullDownShift = u32PullUpShift;

		if (u16Mode & EN_KER_GPIO_PAD_CFG_PULL_UP) {
			if (u32PullUpReg == 0) {
				GPIO_INF_LOG("Pad_%d don't support pull up\n",
					pin);

				return FALSE;
			}
			v = u32PullUpReg;
			if (u16Mode & EN_KER_GPIO_PAD_CFG_EN_PULL_UP)
				v |= (1L<<u32PullUpShift);
			else
				v &= ~(1L<<u32PullUpShift);

			u32PullUpReg = v;
		}

		if (u16Mode & EN_KER_GPIO_PAD_CFG_PULL_DOWN) {
			if (u32PullDownReg == 0) {
				GPIO_INF_LOG("Pad_%d not support pull down\n",
					pin);

				return FALSE;
			}
			v = u32PullDownReg;
			if (u16Mode & EN_KER_GPIO_PAD_CFG_EN_PULL_DOWN)
				v |= (1L<<u32PullDownShift);
			else
				v &= ~(1L<<u32PullDownShift);

			u32PullDownReg = v;
		}
	}

	return TRUE;
}




/* ************************************************************************** */
/*  Function Name: ntgpio_ConfigIntType */
/*  Description:   config int type */
/*  Parameters:    pin - pin id */
/*                 enIntType - int type */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int  ntgpio_ConfigIntType(enum EN_KER_GPIO_PIN pin,
	EN_KER_GPIO_INT_TYPE enIntType)
{
	unsigned int *intEnableReg;
	unsigned int *intLvlReg;
	unsigned int shiftbit;
	unsigned int mask, gpioilv, gpioipl;

	if (g_gpioDeviceOpen == FALSE) {
		/*  device not yet open */
		return FALSE;
	}
	if (pin >= EN_KER_GPIO_PIN_TOTAL) {
		/*  invalid pin */
		return FALSE;
	}

	for (shiftbit = 0;
		shiftbit < ARRAY_SIZE(gstu8Interrupt_Pin_Table); shiftbit++) {
		if (pin == gstu8Interrupt_Pin_Table[shiftbit])
			goto INITIAL_INT;

	}
	GPIO_INF_LOG("\n  Incorrect Int. pin[%d]\n", pin);
	return FALSE;

INITIAL_INT:

	intEnableReg = (unsigned int *)ioremap_nocache(GPIO_INT_EN_REG, 1);
	intLvlReg = (unsigned int *)ioremap_nocache(GPIO_INT_LV_REG, 1);

	down(&g_gpio_sem);
	mask	= (1L<<shiftbit);
	gpioilv		= (*intLvlReg) & 0x3FF;
	gpioipl		= (*intLvlReg) & (0x3FF << 16);
	switch (enIntType) {
	case EN_KER_GPIO_NO_INT:
		{
			*intEnableReg &= ~mask;
			break;
		}
	case EN_KER_GPIO_FALLING_EDGE_INT:
		{
			*intEnableReg |= mask;
			gpioilv &= ~mask;
			gpioipl &= ~(mask << 16);
			*intLvlReg = (gpioilv | gpioipl);
			break;
		}
	case EN_KER_GPIO_RISING_EDGE_INT:
		{
			*intEnableReg |= mask;
			gpioilv &= ~mask;
			gpioipl |= (mask << 16);
			*intLvlReg = (gpioilv | gpioipl);
			break;
		}
	case EN_KER_GPIO_LOW_LEVEL_INT:
		{
			*intEnableReg |= mask;
			gpioilv |= mask;
			gpioipl &= ~(mask << 16);
			*intLvlReg = (gpioilv | gpioipl);
			break;
		}
	case EN_KER_GPIO_HIGH_LEVEL_INT:
		{
			*intEnableReg |= mask;
			gpioilv |= mask;
			gpioipl |= (mask << 16);
			*intLvlReg = (gpioilv | gpioipl);
			break;
		}
	default:
		{
			break;
		}
	}
	up(&g_gpio_sem);

	return TRUE;
}

/* ************************************************************************** */
/*  Function Name: ntgpio_AltModeInit */
/*  Description:   config fun mode of pins */
/*  Parameter:    pAltIniParm - gpio function parameter */
/*                 ulCount - amount of pins */
/*  Returns:       TRUE if successful */
/*                 FALSE if fail */
/* ************************************************************************** */
static int ntgpio_AltModeInit(ST_KER_GPIO_ALT_INIT *pAltIniParm,
	unsigned int ulcount)
{
	unsigned int i;

	if (g_gpioDriverInit == FALSE) {
		/*  driver not yet initial */
		return FALSE;
	}

	if (pAltIniParm == NULL || ulcount == 0) {
		/*  invalid parameter */
		return FALSE;
	}

	for (i = 0; i < ulcount; i++) {
		if (ntgpio_SetAltMode((pAltIniParm + i)->enPin,
					(pAltIniParm + i)->u8enMode) == FALSE)
			GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
			"ntgpio_SetAltMode error!! enPin=%d mode=%d\n",
			(pAltIniParm + i)->enPin, (pAltIniParm + i)->u8enMode);

	}

	return TRUE;
}


static bool gpio_default_function_pin_search(enum EN_KER_GPIO_PIN enPin)
{
	unsigned int i;

	if (enPin >= EN_KER_GPIO_PIN_TOTAL)
		return FALSE;

	/* Set '1' become GPIO function */
	for (i = 0; i < ARRAY_SIZE(gstu8Default_Function_Pin_Table); i++) {

		if (enPin == gstu8Default_Function_Pin_Table[i])
			return TRUE;

	}
	return FALSE;
}

/* ************************************************************************** */
/*  Function Name: gpio_IOMuxSel */
/*  Description:  Check the IO mux data according to pin id */
/*  Parameters:    pin - pin id */
/*  Returns:       pin mux bit no. */
/* ************************************************************************** */
void gpio_IOMuxSel(enum EN_KER_GPIO_PIN pin, unsigned int *pData)
{
	unsigned char funGrp_mask = 0;
	unsigned int ulData;
	EN_DRV_MIPS_GPIO_GROUP enGroup;
	ulData = *pData;

	gpio_group_search(pin, &enGroup);
	funGrp_mask = (pin - 
		gstGPIO_Grp_Info_Tbl[enGroup].u8group_start) % 8;
	ulData &= ~(GPIO_ALT_MUX_TBL[funGrp_mask].u32AltMux);

	if (TRUE == gpio_default_function_pin_search(pin))
		ulData |= EN_GPIO_MUX_SELECT_FUN1 
			<< GPIO_ALT_MUX_TBL[funGrp_mask].u8GrpOffset * 4;

	*pData = ulData;
}

static bool  gpio_group_search(enum EN_KER_GPIO_PIN enPin,
	EN_DRV_MIPS_GPIO_GROUP *penGroup)
{
	if ((enPin >= EN_KER_GPIO_PIN_TOTAL) || (penGroup == NULL))
		return FALSE;

	if (enPin <= EN_KER_GPIO_GPA_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPA;
	else if (enPin <= EN_KER_GPIO_GPB_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPB;
	else if (enPin <= EN_KER_GPIO_GPC_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPC;
	else if (enPin <= EN_KER_GPIO_GPD_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPD;
	else if (enPin <= EN_KER_GPIO_GPE_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPE;
#if	!defined(CONFIG_ARCH_NVT72172)
	else if (enPin <= EN_KER_GPIO_GPH_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPH;
	else if (enPin <= EN_KER_GPIO_GPK_END)
		*penGroup = EN_KER_MIPS_GPIO_GROUP_MIPS_GPK;
#endif
	return TRUE;
}

int nt726xx_Driving_set(struct gpio_chip *chip, unsigned offset, int value)
{
	void				*ptr;
	ST_KER_GPIO_PAD_PARAM *pad;

	GPIO_DBG_LOG("### %s:%d start", __FUNCTION__, __LINE__);
	
	ptr = kmalloc(sizeof(ST_KER_GPIO_PAD_PARAM), GFP_KERNEL);
	if(ptr == NULL)
	{
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
			"Line(%d) NULL ptr!!\n", __LINE__);
		return -ENOTSUPP;
	}
	else
	{
		pad = (ST_KER_GPIO_PAD_PARAM *)ptr;
		pad->enPin = (enum EN_KER_GPIO_PIN)offset;
		pad->u8Driving = (unsigned char)value;
		
		GPIO_DBG_LOG("### %s:%d  pad->enPin = %d, pad->u8Driving = %d \n",
			__FUNCTION__, __LINE__, pad->enPin, pad->u8Driving);

		if( ntgpio_SetDriving(pad->enPin, pad->u8Driving)==FALSE )
		{
			GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
				"Line(%d), set Driving err, enPin(%d), u8Driving(%d)\n",
				__LINE__, pad->enPin, pad->u8Driving);

			kfree(ptr);
			return -ENOTSUPP;
		}
					
		{
			_DRV_GPIO_CFG_CELL_t stItem;
			memset((char*)&stItem, 0x00, sizeof(_DRV_GPIO_CFG_CELL_t));
			stItem.enPinId = pad->enPin;
			stItem.u8Driving = pad->u8Driving;
			_drv_gpio_record_add_p (GPIO_CMD_DRIVING, FALSE, (void*)&stItem, 1);
		}	
			kfree(ptr);
			return 0;
	}
	
}
EXPORT_SYMBOL(nt726xx_Driving_set);

int nt726xx_Driving_get(struct gpio_chip *chip, unsigned offset, unsigned char *getDriving)
{
	int ret = 0;
	void				*ptr;
	ST_KER_GPIO_OP_PARAM	*readdata;

	GPIO_DBG_LOG("### %s:%d start", __FUNCTION__, __LINE__);

	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);
	if(ptr == NULL)
	{
		return -ENOTSUPP;
	}
	else
	{
		readdata = (ST_KER_GPIO_OP_PARAM *)ptr;
		readdata->enPin = (enum EN_KER_GPIO_PIN)offset;

		if(ntgpio_GetDriving(readdata->enPin, getDriving) == FALSE)
		{
			ret = -ENOTSUPP;
			goto END_DRIVING_GET;
		}
		GPIO_DBG_LOG("### %s:%d get driving = %d ",
			__FUNCTION__, __LINE__, *getDriving);
	}
END_DRIVING_GET:
	kfree(ptr);
	return ret;
}
EXPORT_SYMBOL(nt726xx_Driving_get);


/*pinctrl*/

int nt726xx_ConfigPadMode_set(struct gpio_chip *chip,
	unsigned offset, int value)
{
	void				*ptr;
	ST_KER_GPIO_PAD_PARAM *pad;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_PAD_PARAM), GFP_KERNEL);

	if (ptr == NULL) {
		/* return -ENOTSUPP; */
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR" error!!\n");
		return -ENOTSUPP;
	}

	pad = (ST_KER_GPIO_PAD_PARAM *)ptr;
	pad->enPin = (enum EN_KER_GPIO_PIN)offset;

	pad->u16Mode = (unsigned short)value;
	GPIO_DBG_LOG("nt726xx_ConfigPadMode_set mode = %d\n",
			pad->u16Mode);
	down(&g_gpio_sem);
	if (ntgpio_ConfigPadMode(pad->enPin, pad->u16Mode) == FALSE) {
			GPIO_INF_LOG(
			""GPIO_MSG_PREFIX_ERR"error!!	Pin[%d], Mode=[%d]\n",
				pad->enPin, pad->u16Mode);
			kfree(ptr);
			up(&g_gpio_sem);
			return -ENOTSUPP;
	}
	{
			_DRV_GPIO_CFG_CELL_t stItem;

			memset((char *)&stItem, 0x00,
				sizeof(_DRV_GPIO_CFG_CELL_t));

			stItem.enPinId = pad->enPin;
			stItem.u16PadMode = pad->u16Mode;
			_drv_gpio_record_add_p(GPIO_CMD_PADMODE,
				FALSE, (void *)&stItem, 1);
	}
	up(&g_gpio_sem);
	kfree(ptr);
	return 0;


}
EXPORT_SYMBOL(nt726xx_ConfigPadMode_set);


int nt726xx_pinctrl_get(struct gpio_chip *chip, unsigned offset)
{
	void				*ptr;
	ST_KER_GPIO_OP_PARAM	*readdata;
	int						u8func_mode;

	GPIO_DBG_LOG("###  start\n");

	/* get the pin level */

		ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);

	if (ptr == NULL)
		return -ENOTSUPP;

	readdata = (ST_KER_GPIO_OP_PARAM *)ptr;
	readdata->enPin = (enum EN_KER_GPIO_PIN)offset;

	if (ntgpio_GetAltMode(readdata->enPin, &(readdata->u8Value)) == FALSE) {
		kfree(ptr);
		return -ENOTSUPP;
	}
	u8func_mode = (int)(readdata->u8Value);
	kfree(ptr);
	GPIO_DBG_LOG("###  get u8func_mode = %d\n", u8func_mode);
	return u8func_mode;
}
EXPORT_SYMBOL(nt726xx_pinctrl_get);


int nt726xx_pinctrl_set(struct gpio_chip *chip, unsigned offset, int value)
{
	void				*ptr;
	ST_KER_GPIO_OP_PARAM	*writedata;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);

	if (ptr == NULL) {
		/* return -ENOTSUPP; */
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
		"nt726xx_pinctrl_get_ptr error!!\n");
		return -ENOTSUPP;
	}

	writedata = (ST_KER_GPIO_OP_PARAM *)ptr;
	writedata->enPin = (enum EN_KER_GPIO_PIN)offset;
	writedata->u8Value = (unsigned char)value;
	down(&g_gpio_sem);
	if (ntgpio_SetAltMode(writedata->enPin, writedata->u8Value) == FALSE) {
		GPIO_INF_LOG("mode error!!, enPin=[%d], u8Value=[%d]\n",
				writedata->enPin, writedata->u8Value);

		kfree(ptr);
		up(&g_gpio_sem);
		return -ENOTSUPP;
	}
	{
		_DRV_GPIO_CFG_CELL_t stItem;

		memset((char *)&stItem, 0x00,
			sizeof(_DRV_GPIO_CFG_CELL_t));

		stItem.enPinId = writedata->enPin;
		stItem.enAltMode = writedata->u8Value;
		_drv_gpio_record_add_p(GPIO_CMD_ALTMOD,
			FALSE, (void *)&stItem, 1);
	}
	up(&g_gpio_sem);
	kfree(ptr);
	return 0;
}
EXPORT_SYMBOL(nt726xx_pinctrl_set);


int nt726xx_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	void                *ptr;
	ST_KER_GPIO_OP_PARAM *config;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);

	if (ptr == NULL)
		return -ENOTSUPP;

	config = (ST_KER_GPIO_OP_PARAM *)ptr;
	config->enPin = (enum EN_KER_GPIO_PIN)offset;
	down(&g_gpio_sem);
	if (ntgpio_OpenGPIO(config->enPin, TRUE) == FALSE) {
		kfree(ptr);
		up(&g_gpio_sem);
		return -ENOTSUPP;
	}
	{
		_DRV_GPIO_CFG_CELL_t stItem;

		memset((char *)&stItem, 0x00,
			sizeof(_DRV_GPIO_CFG_CELL_t));

		stItem.enPinId = config->enPin;
		stItem.bGpioMode = TRUE;
		_drv_gpio_record_add_p(GPIO_CMD_OPEN,
				FALSE, (void *)&stItem, 1);
	}
	up(&g_gpio_sem);
	kfree(ptr);
	return 0;
}
EXPORT_SYMBOL(nt726xx_gpio_request);


void nt726xx_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	enum EN_KER_GPIO_PIN pin;

	GPIO_DBG_LOG("###  start\n");

	if ((enum EN_KER_GPIO_PIN)offset >= EN_KER_GPIO_PIN_TOTAL) {
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
			"nt726xx_gpio_free_get_offset error!!\n");
	} else {
		pin = ((enum EN_KER_GPIO_PIN)offset);
		ntgpio_CloseGPIO(pin);
	}
}
EXPORT_SYMBOL(nt726xx_gpio_free);


int nt726xx_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	void *ptr;
	ST_KER_GPIO_OP_PARAM *writedata;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);
	if (ptr == NULL)
		return -ENOTSUPP;

	writedata = (ST_KER_GPIO_OP_PARAM *)ptr;
	writedata->enPin = (enum EN_KER_GPIO_PIN)offset;
	down(&g_gpio_sem);
	if (ntgpio_SetDirection(writedata->enPin, TRUE) == FALSE) {
		kfree(ptr);
		up(&g_gpio_sem);
		return -ENOTSUPP;
	}
	{
		_DRV_GPIO_CFG_CELL_t stItem;

		memset((char *)&stItem, 0x00,
			sizeof(_DRV_GPIO_CFG_CELL_t));

		stItem.enPinId = writedata->enPin;
		stItem.enDirection = TRUE;
		_drv_gpio_record_add_p(GPIO_CMD_DIRECTION,
				FALSE, (void *)&stItem, 1);
	}
	up(&g_gpio_sem);
	kfree(ptr);
	return 0;
}
EXPORT_SYMBOL(nt726xx_gpio_direction_in);


int nt726xx_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	void *ptr;
	ST_KER_GPIO_OP_PARAM *writedata;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);

	if (ptr == NULL)
		return -ENOTSUPP;

	writedata = (ST_KER_GPIO_OP_PARAM *)ptr;
	writedata->enPin = (enum EN_KER_GPIO_PIN)offset;

	if (value)
		writedata->b8Value = TRUE;
	else
		writedata->b8Value = FALSE;

	down(&g_gpio_sem);
	if (ntgpio_SetDirection(writedata->enPin, FALSE) == FALSE) {
		kfree(ptr);
		up(&g_gpio_sem);
		return -ENOTSUPP;
	}

	if (ntgpio_SetLevel(writedata->enPin, writedata->b8Value) == FALSE) {
		kfree(ptr);
		up(&g_gpio_sem);
		return -ENOTSUPP;
	}
	{
		_DRV_GPIO_CFG_CELL_t stItem;

		memset((char *)&stItem, 0x00,
			sizeof(_DRV_GPIO_CFG_CELL_t));

		stItem.enPinId = writedata->enPin;
		stItem.enDirection = FALSE;
		stItem.enLevel = writedata->b8Value;
		_drv_gpio_record_add_p(GPIO_CMD_DIRECTION,
				FALSE, (void *)&stItem, 1);

		_drv_gpio_record_add_p(GPIO_CMD_LEVEL,
				FALSE, (void *)&stItem, 1);
	}
	up(&g_gpio_sem);
	kfree(ptr);
	return 0;
}
EXPORT_SYMBOL(nt726xx_gpio_direction_out);


int nt726xx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	void				*ptr;
	ST_KER_GPIO_OP_PARAM	 *readdata;

	GPIO_DBG_LOG("###  start\n");

	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);
	if (ptr == NULL)
		return -ENOTSUPP;

	readdata = (ST_KER_GPIO_OP_PARAM *)ptr;
	readdata->enPin = (enum EN_KER_GPIO_PIN)offset;

	if (ntgpio_GetLevel(readdata->enPin, &(readdata->b8Value)) == FALSE) {
		kfree(ptr);
		return -ENOTSUPP;
	}

	if (readdata->b8Value) {
		kfree(ptr);
		return 1;
	}
	kfree(ptr);
	return 0;

}
EXPORT_SYMBOL(nt726xx_gpio_get);


void nt726xx_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	void				*ptr;
	ST_KER_GPIO_OP_PARAM *writedata;

	GPIO_DBG_LOG("###  start\n");
	ptr = kmalloc(sizeof(ST_KER_GPIO_OP_PARAM), GFP_KERNEL);

	if (ptr == NULL) {
		GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
					"nt726xx_gpio_get_ptr error!!\n");
	} else {

		writedata = (ST_KER_GPIO_OP_PARAM *)ptr;
		writedata->enPin = (enum EN_KER_GPIO_PIN)offset;
		if (value)
			writedata->b8Value = TRUE;
		else
			writedata->b8Value = FALSE;

		down(&g_gpio_sem);
		if (ntgpio_SetLevel(writedata->enPin,
			writedata->b8Value) == FALSE)
				GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
					"error! enPin=[%d], b8Value=[%d]\n",
					writedata->enPin, writedata->b8Value);

		{
			_DRV_GPIO_CFG_CELL_t stItem;

			memset((char *)&stItem, 0x00,
				sizeof(_DRV_GPIO_CFG_CELL_t));

			if ((writedata != NULL) && (ptr != NULL)) {
				stItem.enPinId = writedata->enPin;
				stItem.enDirection = FALSE;
				stItem.enLevel = writedata->b8Value;
				_drv_gpio_record_add_p(GPIO_CMD_LEVEL,
					FALSE, (void *)&stItem, 1);
			} else {
				kfree(ptr);
				GPIO_INF_LOG(""GPIO_MSG_PREFIX_ERR
					"nt726xx_gpio_set_level error!!\n");
			}
		}
		up(&g_gpio_sem);
		kfree(ptr);
	}

}
EXPORT_SYMBOL(nt726xx_gpio_set);

static int ConvertGpioIndexToHwPin(unsigned int  index)
{
	int res = EN_KER_GPIO_PIN_NULL;
	int i = 0;
	int j = sizeof(g_stGPIO_KTbl_ker) /
		sizeof(ST_GPIO_DESCRIPTION_KER);

	for (; i < j; i++) {
		if (index == g_stGPIO_KTbl_ker[i].u8Function) {
			res = g_stGPIO_KTbl_ker[i].enPin;
			goto TRESULT;
		}
	}
TRESULT:
	return res;
}


void Suspend_GPIO_Init(void)
{
	GPIO_DBG_LOG("[%s(): %d]\n", __func__, __LINE__);
	/* Do Nothing yet. If it needs to initialize,
	SEC will use this function. */
}
EXPORT_SYMBOL(Suspend_GPIO_Init);

void Suspend_GPIO_Read(unsigned int u8IndexGPIO, unsigned char *u8Data)
{
	bool level = FALSE;
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;

	unsigned int enpin;

	enpin = ConvertGpioIndexToHwPin(u8IndexGPIO);

	GPIO_DBG_LOG("[%s(): %d] u8IndexGPIO = %d, u8Data = %d\n",
		__func__, __LINE__, u8IndexGPIO, *u8Data);
	pstMetaData = _drv_gpio_load_p();
	if (pstMetaData && u8Data) {
		if (ntgpio_GetLevel(enpin, &level) == FALSE)
			pr_err("[%s] ntgpio_GetLevel FALSE\n", __func__);
		*u8Data = (level == TRUE ? TRUE:FALSE);
	} else {
		pr_err("[%s] NULL DATA\n", __func__);
	}
}
EXPORT_SYMBOL(Suspend_GPIO_Read);
void Suspend_GPIO_Write(unsigned int u8IndexGPIO, unsigned char u8Data)
{
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;

	unsigned int enpin;

	enpin = ConvertGpioIndexToHwPin(u8IndexGPIO);
	GPIO_DBG_LOG("[%s(): %d] u8IndexGPIO = %d, u8Data = %d\n",
		__func__, __LINE__, u8IndexGPIO, u8Data);
	pstMetaData = _drv_gpio_load_p();
	if (pstMetaData) {
		if (ntgpio_SetLevel(enpin, (u8Data == 0 ? 0 : 1)) == FALSE)
			pr_err("[%s] ntgpio_SetLevel FALSE\n", __func__);
	}
}
EXPORT_SYMBOL(Suspend_GPIO_Write);
int Suspend_GPIO_Recovery(unsigned int gpionum, bool level)
{
	int iRet = -1;
	void *ptr;
	_DRV_GPIO_CTRL_t *pstMetaData = NULL;
	struct _recovery_item_t *pstNew = NULL, *pstCurr = NULL;

	pstMetaData = _drv_gpio_load_p();
	GPIO_DBG_LOG("[%s(): %d] gpionum = %d, level = %d\n",
		__func__, __LINE__, gpionum, level);
	if (pstMetaData) {
		down(&pstMetaData->stSema);

		ptr = kmalloc(sizeof(struct _recovery_item_t), GFP_KERNEL);
		pstNew = (struct _recovery_item_t *)ptr;
		if (pstNew) {
			pstNew->u16Pin = gpionum;
			pstNew->u8Level = level;
			pstNew->pstNext = NULL;
			pstCurr = pstMetaData->pstRecoveryList;

			if (!pstMetaData->pstRecoveryList) {
				pstMetaData->pstRecoveryList = pstNew;
			} else {
				while (pstCurr->pstNext)
					pstCurr = pstCurr->pstNext;

				pstCurr->pstNext = pstNew;
			}
		}
		up(&pstMetaData->stSema);
		iRet = 0;
	}

	return iRet;
}
EXPORT_SYMBOL(Suspend_GPIO_Recovery);

int suspend_gpio_recovery(unsigned int gpionum, bool level)
{
	return Suspend_GPIO_Recovery(gpionum, level);
}
EXPORT_SYMBOL(suspend_gpio_recovery);



static struct gpio_chip template_chip = {
	.label			= "nt726xx",
	.owner			= THIS_MODULE,
	.request		= nt726xx_gpio_request,
	.free			= nt726xx_gpio_free,
	.direction_input	= nt726xx_gpio_direction_in,
	.get			= nt726xx_gpio_get,
	.direction_output	= nt726xx_gpio_direction_out,
	.set			= nt726xx_gpio_set,
};

static const struct of_device_id nt726xx_gpio_match[];

static int nt726xx_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const struct of_device_id *match;
	const struct nt726xx_gpio_platform_data *pdata;
	struct resource *res;
	struct gpio_bank *bank;
	static int gpio;


	match = of_match_device(of_match_ptr(nt726xx_gpio_match), dev);

	pdata = match ? match->data : dev->platform_data;

	if (!pdata)
		return -EINVAL;

	GPIO_DBG_LOG("#### GPIO: nt726xx_gpio_probe_match\r\n");

	bank = devm_kzalloc(dev, sizeof(struct gpio_bank), GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	bank->dev = dev;
	bank->width = pdata->bank_width;
	bank->non_wakeup_gpios = pdata->non_wakeup_gpios;
	bank->regs = pdata->regs;

	if (node) {
		if (!of_property_read_bool(node, "nvt,gpio-always-on")) {
			bank->loses_context = true;
				GPIO_DBG_LOG("###  loses_context = True.\n");
			}
	} else {
		bank->loses_context = pdata->loses_context;

		if (bank->loses_context)
			bank->get_context_loss_count =
				pdata->get_context_loss_count;
	}

	if (bank->regs->set_dataout && bank->regs->clr_dataout)
		bank->set_dataout = NULL;
	else
		bank->set_dataout = NULL;

	spin_lock_init(&bank->lock);


	/* Static mapping, never released */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		dev_err(dev, ""GPIO_MSG_PREFIX_ERR"Invalid mem resource\n");
		/* irq_domain_remove(bank->domain); */
		return -ENODEV;
	}

	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				     pdev->name)) {
		dev_err(dev, "Region already claimed\n");
		/* irq_domain_remove(bank->domain); */
		return -EBUSY;
	}

	bank->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!bank->base) {
		dev_err(dev, ""GPIO_MSG_PREFIX_ERR"Could not ioremap\n");
		/* irq_domain_remove(bank->domain); */
		return -ENOMEM;
	}

	/*dts info*/
	GPIO_DBG_LOG(" resource=%pR, start=0x%x \r\n", res, res->start);
	GPIO_DBG_LOG(" name:%s, base:0x%lx\n",
			res->name, (unsigned long)bank->base);

	bank->chip = template_chip;
	bank->chip.label = "gpio";
	bank->chip.base = gpio;
	gpio += bank->width;

	bank->chip.ngpio = bank->width;

	#ifdef CONFIG_OF_GPIO
	bank->chip.of_node = of_node_get(node);
	GPIO_DBG_LOG("###  CONFIG_OF_GPIO be opened.\n");
	#endif


	gpiochip_add(&bank->chip);

	platform_set_drvdata(pdev, bank);
	GPIO_DBG_LOG("#### GPIO: nt726xx_gpio_probe_set_drvdata\r\n");

	list_add_tail(&bank->node, &nt726xx_gpio_list);

	return 0;
}

static int nt726xx_gpio_remove(struct platform_device *pdev)
{
	KER_GPIO_DrvExit();
	return 0;
}

static struct nt726xx_gpio_reg_offs nt726xx_gpio_regs = {
	.revision =			0x0,
	.direction =		0x0,
	.datain =			0x0,
	.dataout =			0x0,
	.set_dataout =		0x0,
	.clr_dataout =		0x0,
	.irqstatus =		0x0,
	.irqstatus2 =		0x0,
	.irqenable =		0x0,
	.irqenable2 =		0x0,
	.set_irqenable =	0x0,
	.clr_irqenable =	0x0,
	.debounce =			0x0,
	.debounce_en =		0x0,
	.ctrl =				0x0,
	.wkup_en =			0x0,
	.leveldetect0 =		0x0,
	.leveldetect1 =		0x0,
	.risingdetect =		0x0,
	.fallingdetect =	0x0,
};

static const struct nt726xx_gpio_platform_data nt726xx_pdata = {
	.regs = &nt726xx_gpio_regs,
	/*.bank_width = 32,*/
	.bank_width = 256,
};

static const struct of_device_id nt726xx_gpio_match[] = {
	{
		.compatible = "nvt,nt726xx-gpio",
		.data = &nt726xx_pdata,
	},
	{ },
};


static struct platform_driver nt726xx_gpio_driver = {
	.driver.name	= "nt726xx-gpio",
	.driver.owner	= THIS_MODULE,
	.driver.pm		= &_ker_gpio_pm_ops,
	.driver.of_match_table = of_match_ptr(nt726xx_gpio_match),
	.probe		= nt726xx_gpio_probe,
	.remove		= nt726xx_gpio_remove,
};

int __init nt726xx_gpio_init(void)
{
	int ret;

	GPIO_DBG_LOG("#### GPIO: nt726xx_gpio_init_.\r\n");

	/* Ker_GPIO_DbgMenu(); */

	ret = KER_GPIO_DrvInit();
	if (ret < 0) {
		GPIO_DBG_LOG("Can't register GPIO Driver, ret = %d\n"
				, ret);

		goto err_dp;
	}
	ret = platform_driver_register(&nt726xx_gpio_driver);
	if (ret < 0)
		return ret;

	return 0;
err_dp:
    /* dummy dinit */
	return 0;
}
/* subsys_initcall(nt726xx_gpio_init); */

void __exit nt726xx_gpio_exit(void)
{
	platform_driver_unregister(&nt726xx_gpio_driver);
}

postcore_initcall(nt726xx_gpio_init);

module_exit(nt726xx_gpio_exit);


MODULE_AUTHOR("NOVATEK");
MODULE_DESCRIPTION("NOVATEK NT726xx gpio driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_ALIAS("platform:" DRIVER_NAME);

