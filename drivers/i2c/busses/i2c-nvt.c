/*
 * I2C master mode driver for Novatek Nt726xx
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License.
 */

/* Standard */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/version.h>
/*#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
#include <linux/of_i2c.h>
#endif*/
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/semaphore.h>
#include <linux/irqdomain.h>
#include <linux/timer.h>/*20170118 Test IRQ*/

/* Driver */
#include "i2c-nvt.h"
#include <linux/ker_iic_syscalls.h>


/* ******************************************************** */
/* Local Constant Definitions                               */
/* ******************************************************** */
/*#define IRQF_TRIGGER_HIGH	0x00000004*/
/* Driver name and version */

#define FLUSH_REG(reg_val) {u32dummy = ((u32)(reg_val)); }

#define DRIVER_NAME		"nt726xx_i2c"
/* -------------------------------
v1.0: original version
v1.1: For ErrMsg output standard format
--------------------------------*/
#define DRIVER_VERSION	"1.1"



/* Timeout waiting for the controller to respond */
#define I2C_TIMEOUT		(msecs_to_jiffies(1000))

#define TRYDTS 1
#define BUS_NAME_INDEX 12
#define DEBUG
#define TMP_DISABLE 1
#define NUM_OF_I2C_MASTER 6
#define BURST_PATCH 1
/*Tuner Nreset use start*/
#define GPIO_HPD_HIGH    1
#define GPIO_HPD_LOW     0
#define Tuner_Nreset	15
/*Tuner Nreset use end*/
/* NT726xx I2C device structure */
struct nt726xx_i2c_dev {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct i2c_adapter	*adapter;
	struct completion	msg_complete;

	u16			msg_err;	/* message errors */
	struct i2c_msg		*msg;
	u32 bus_clk_rate;
	/* platform data */
	unsigned char		speed;
	unsigned char		filter;
};
struct timer_list IRQ_report_timer;/*20170118 Test IRQ*/
static u32 g_iic_irq_num;
static void I2C_IRQ_init_timer(void);
static void I2C_IRQ_report(unsigned long arg);
static void _i2c_reset_slaveDev(int bus);/*for Tuner/AMP timeout*/

/* ******************************************************** */
/* Local Types Declarations                                 */
/* ******************************************************** */

/* ******************************************************** */
/* Local Function Protype                                   */
/* ******************************************************** */
static u32  __rtnFilterSubAddr(u8 *subAdrBuf,
	enum EN_KER_IIC_SUBADDR_MODE selectMode);
static enum EN_KER_IIC_CLOCK __rtnFilterBusClock(u32 bus_clk_rate);
static void __iomem *__rtnBaseAddr(int masterId);
static inline void nt726xx_i2c_wr_reg(int masterId, int reg, u32 val);
static inline u32 nt726xx_i2c_rd_reg(int masterId, int reg);
static inline void nt726xx_i2c_clear_int(struct nt726xx_i2c_dev *dev, u32 msk);
static inline void nt726xx_i2c_enable(struct nt726xx_i2c_dev *dev);
static inline void nt726xx_i2c_disable(struct nt726xx_i2c_dev *dev);
static void _busReset(int masterId);
static bool ntiic_bus_init(struct platform_device *pdev,
	enum EN_KER_IIC_CLOCK bus_clk, enum EN_KER_IIC_DUTY_LEVEL bus_duty,
	enum EN_KER_IIC_ACK_CTRL enAckCtrl, u8 enAckCtrlCounter);
static bool ntiic_set_clk(enum EN_KER_IIC_CLOCK bus_clk);
static u32 ntiic_count_timeout(struct i2c_adapter *adap,
	enum EN_KER_IIC_CLOCK bus_clk, u32 u32NumOfDataBytes);
static inline short nt726xx_i2c_fifo_flush(struct nt726xx_i2c_dev *dev);
static int nt726xx_i2c_hwinit(struct platform_device *pdev, int channel);
static short nt726xx_i2c_wait_bus_release(struct nt726xx_i2c_dev *dev);
static int rtnFIFO_Reg(u32 fifoIdx, u32 offset);
static irqreturn_t nt726xx_i2c_isr(int irq, void *dev_info);
static int nt726xx_i2c_xfer_rd(struct nt726xx_i2c_dev *i2c_dev,
	struct i2c_adapter *adap, struct i2c_msg *pmsg,
	struct ST_KEEP_MSGINFO stKpt, bool stop);
static int nt726xx_i2c_xfer_wr(struct nt726xx_i2c_dev *i2c_dev,
	struct i2c_adapter *adap, struct i2c_msg *pmsg,
	struct ST_KEEP_MSGINFO stKpt, bool stop);
static int nt726xx_i2c1_xfer(struct i2c_adapter *adap,
	struct i2c_msg msgs[], int num);
static u32 nt726xx_i2c1_func(struct i2c_adapter *adap);
static int _nt726xx_init_md(struct platform_device *pdev);
static int _nt726xx_rm_md(struct platform_device *pdev);
/*int __init nt726xx_i2c_probe(struct platform_device *pdev);
int __exit nt726xx_i2c_remove(struct platform_device *pdev);*/

/* ******************************************************** */
/* Local Global Variables */
/* ******************************************************** */
static struct semaphore				g_iic_sem_HW[NUM_OF_I2C_MASTER];
static struct semaphore				g_iic_sem;
static struct ST_I2C_INT_MAILBOX gstI2CIntMailBox[NUM_OF_I2C_MASTER];
/*static volatile unsigned long u32dummy;*/
static unsigned long u32dummy;
static u8 gChannel;/* = 0;//do not initialise globals to 0 or NULL*/
static u32 gu32hclk_iic;
static u8 gbirq_init = FALSE;
static u8 gHw_init = FALSE;
static u8 gResumeEn = FALSE;
static u8 gSuspendEn = FALSE;
static u8 gProcCNT = NUM_OF_I2C_MASTER;
static int ackcontrolcounter[5] = {0x34, 0x36, 0x38, 0x3a, 0x3c};

static void __iomem *reg0, *reg1, *reg2, *reg3, *reg4, *reg5;
static bool IS_IIC_PM_INIT = FALSE;

struct nt726xx_i2c_dev *g_i2c;/* = NULL;//do not initialise to 0 or NULL*/
struct i2c_adapter *g_adap;/* = NULL;//do not initialise to 0 or NULL*/

/*it will use resume/suspend function of I2C standard.*/
static int _ker_iic_suspend_p(struct device *pstDevice);
static int _ker_iic_resume_p(struct device *pstDevice);

/*for mips hw i2c.1*/
static struct ST_HWI2C   gstHWI2C = {
	.wait      = __WAIT_QUEUE_HEAD_INITIALIZER(gstHWI2C.wait),
	.irq_init  = 0,
	.name      = "NT72668-IIC.1",
};

/* I2C algorithm structure */
static struct i2c_algorithm nt726xx_i2c_algo = {
	.master_xfer = nt726xx_i2c1_xfer,
	.functionality = nt726xx_i2c1_func,
};

static const struct dev_pm_ops nt726xx_i2c_pm_ops = {/*20160722 MZ*/
	/*SET_RUNTIME_PM_OPS(nt726xx_i2c_runtime_suspend,*/
	/*		   nt726xx_i2c_runtime_resume,           */
	/*		   NULL)                                 */
	/*.resume = _ker_iic_resume_p,
	.suspend = _ker_iic_suspend_p,*//*20161215 MZ*/
	.resume_early = _ker_iic_resume_p,
	.suspend_late = _ker_iic_suspend_p,

};


/* ********************************************************* */
/* Extern Function Prototype                                 */
/* ********************************************************* */
/* ********************************************************* */
/* Extern Global Variables                                   */
/* ********************************************************* */
/*extern unsigned long hclk;*/
static unsigned long hclk = 96000000;/*20160711 move to kernel and change path*/
/* ********************************************************* */
/* Interface Functions                                       */
/* ********************************************************* */

/* ********************************************************* */
/* Goal : (STR) Fast boot, Sync from 14U*/
/* Modified : 2015/04/14*/
/* Author : Anderson*/
/* ********************************************************* */

#define KER_IIC_PM_ENABLE		1

struct _drv_iic_status_rec_t {
	struct ST_KER_IIC_OPEN_PARAM		stOpenParam;
	struct _drv_iic_status_rec_t *pstNext;
};
/*20160711 move kernel to change path*/
struct _ker_iic_pmi_ctrl_t {
	int							iRunSuspend;
	int							iInSuspend;
	int							iInComplete;
	int							iInPrepare;
	int							iPreInit;
	wait_queue_head_t				stWaitQ;
	/*KER_PMI_CMD_en				enCmdStatus;*/
	struct semaphore				stSema;

	struct _drv_iic_status_rec_t *pstI2CList;

};

static struct _ker_iic_pmi_ctrl_t *_ker_iic_load_p(void);

/*static void _ker_iic_lock_p (void);*/
/*static void _ker_iic_unlock_p (void);*/
/*it will use resume/suspend function of I2C standard.*/
static void _ker_iic_pm_init_p(void);

static struct _ker_iic_pmi_ctrl_t g_stPMIOfI2C;
/*
static const struct dev_pm_ops _ker_iic_pm_ops = {
	.resume = _ker_iic_resume_p,
	.suspend = _ker_iic_suspend_p,
};

static struct bus_type _ker_iic_bus = {
	.name	= "kiic",
	.pm		= &_ker_iic_pm_ops,
};

struct device _ker_iic_dev = {
	.bus = &_ker_iic_bus,
};
*/
static struct _ker_iic_pmi_ctrl_t *_ker_iic_load_p(void)
{
	return &g_stPMIOfI2C;
}
#if 1
static void _ker_iic_record_del_p(int iId)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;
	struct _drv_iic_status_rec_t *pstCurrItem = NULL, *pstPreItem = NULL;
	struct ST_KER_IIC_OPEN_PARAM *pstIIC = NULL;

	pstMetaData = _ker_iic_load_p();
	if (!pstMetaData)
		goto END_DEL;

	pstCurrItem = pstMetaData->pstI2CList;
	while (pstCurrItem) {
		pstIIC = (struct ST_KER_IIC_OPEN_PARAM *)
			&pstCurrItem->stOpenParam;
		if ((int)pstIIC->enBusID == iId) {
			if (!pstPreItem)
				pstMetaData->pstI2CList = pstCurrItem->pstNext;
			else
				pstPreItem->pstNext = pstCurrItem->pstNext;

			pstCurrItem->pstNext = NULL;
			kfree(pstCurrItem);

			break;
		}

		pstPreItem = pstCurrItem;
		pstCurrItem = pstCurrItem->pstNext;
	}

END_DEL:
	return;
}

static void _ker_iic_record_add_p(void *vpParam)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;
	struct _drv_iic_status_rec_t *pstNewItem = NULL, *pstCurrItem = NULL;

	pstMetaData = _ker_iic_load_p();
	if (!pstMetaData) {
		pstNewItem = NULL;
		goto END_ADD;
	}

#if (KER_IIC_PM_ENABLE)
	pstNewItem = kmalloc(sizeof(struct _drv_iic_status_rec_t), GFP_KERNEL);
	if (pstNewItem) {
		memcpy((char *)&pstNewItem->stOpenParam,
			(char *)vpParam, sizeof(struct ST_KER_IIC_OPEN_PARAM));
		pstNewItem->pstNext = NULL;

		_ker_iic_record_del_p((int)pstNewItem->stOpenParam.enBusID);

		pstCurrItem = pstMetaData->pstI2CList;
		if (!pstMetaData->pstI2CList) {
			pstMetaData->pstI2CList = pstNewItem;
		} else {
			while (pstCurrItem->pstNext)
				pstCurrItem = pstCurrItem->pstNext;

			pstCurrItem->pstNext = pstNewItem;
		}
	}
#endif

END_ADD:
	return;

}
#endif

static int _ker_iic_suspend_p(struct device *dev)
{
	unsigned long ulTimeout = 0, ulIRQreg = 0;
	u8 i = 0;
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

	pr_crit("%s: START	gProcCNT=%d\n", __func__, gProcCNT);
	I2C_DBG_LOG("I2C Suspend gResEn=%d gSusEn=%d gProcCNT=%d\n",
			gResumeEn, gSuspendEn, gProcCNT);
	gProcCNT -= 1;
	/*Resume/Suspend opreate in linux kernel, run probe only one.*/
	if ((gSuspendEn == FALSE) && (gProcCNT == 0)) {
		pstMetaData = _ker_iic_load_p();
		if (!pstMetaData)
			goto END_SUSPEND;

		I2C_DBG_LOG("[%s():%d][A0]\n", __func__, __LINE__);

		pstMetaData->iRunSuspend = TRUE;
		pstMetaData->iPreInit = FALSE;
		pstMetaData->iInSuspend = TRUE;
		I2C_DBG_LOG("[%s():%d][A1]\n", __func__, __LINE__);

		/*1s : debug*/
		while (i < NUM_OF_I2C_MASTER) {
			ulIRQreg = nt726xx_i2c_rd_reg(
				i, _REG_INTCTRL) & 0x00001F00;
			if (ulIRQreg)
				ulTimeout = wait_event_timeout(
					pstMetaData->stWaitQ,
					pstMetaData->iInComplete, HZ * 1);

			i++;
		}

		I2C_DBG_LOG("[%s():%d][A2]\n",
			__func__, __LINE__);
		pstMetaData->iInPrepare = FALSE;
		pstMetaData->iInComplete = FALSE;
		if (ulTimeout)
			I2C_DBG_LOG("Timeout\n");

		if (gstHWI2C.irq_init) {
			I2C_DBG_LOG("Disable/Free IRQ\n");
			disable_irq(gstHWI2C.irq_num);
			/*No need to free irq, just disable/enable
			in suspend/resume stage*/
			/*free_irq (gstHWI2C.irq_num, &gstI2CIntMailBox);*/
			gstHWI2C.irq_init = 0;
		}

		/*If timeout is true,
		irq should be disable before clearing status.*/
		while (i < NUM_OF_I2C_MASTER) {
			nt726xx_i2c_wr_reg(
			i, _REG_INTCTRL,
			ulIRQreg | (0x00001F00 << 8));/*20170118 Test IRQ*/

			i++;
		}
		I2C_DBG_LOG("[%s():%d][3]\n",
			__func__, __LINE__);
		pr_crit("%s: END OK\n", __func__);
		gResumeEn = FALSE;
		gSuspendEn = TRUE;
		gProcCNT = NUM_OF_I2C_MASTER;
	}

END_SUSPEND:
	return 0;
}

static int _ker_iic_resume_p(struct device *dev)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;
	struct _drv_iic_status_rec_t *pstCurrItem = NULL;

	pr_crit("%s: START\n", __func__);
	I2C_DBG_LOG("@@@@@@I2C resume gResumeEn=%d  gSuspendEn = %d\n",
		gResumeEn, gSuspendEn);
	/*Resume/Suspend opreate in linux kernel, run probe only one.*/
	if (gResumeEn == FALSE) {
		pstMetaData = _ker_iic_load_p();
		if (!pstMetaData)
			goto END_RESUME;

		pstMetaData->iPreInit = TRUE;
		pstMetaData->iInSuspend = FALSE;
		pstCurrItem = pstMetaData->pstI2CList;
		while (pstCurrItem) {
			/*ntiic_Open (&pstCurrItem->stOpenParam);*/
			pstCurrItem = pstCurrItem->pstNext;
		}
		I2C_DBG_LOG("@@@@@@I2C resume End\n");

		I2C_DBG_LOG("enable I2C IRQ = %d\n", gstHWI2C.irq_num);
		enable_irq(gstHWI2C.irq_num);
		gstHWI2C.irq_init = 1;
		pr_crit("%s: END\n", __func__);
		gResumeEn = TRUE;
		gSuspendEn = FALSE;
	}

END_RESUME:
	return 0;
}
/*
static int _ker_iic_prepare_p(struct device *dev)
{
	struct _ker_iic_pmi_ctrl_t* pstMetaData = NULL;
	pr_debug("Start\n");
	pstMetaData = _ker_iic_load_p();
	if (!pstMetaData) {
		goto END_PREPARE;
	}

	pstMetaData->iInPrepare = TRUE;

END_PREPARE:
	return 0;
}*/
#if 0
static void _ker_iic_lock_p(void)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

	pstMetaData = _ker_iic_load_p();
	if (pstMetaData)
		down(&pstMetaData->stSema);
}

static void _ker_iic_unlock_p(void)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

	pstMetaData = _ker_iic_load_p();
	if (pstMetaData)
		up(&pstMetaData->stSema);
}
#endif
/*it will use resume/suspend function of I2C standard.*/
static void _ker_iic_pm_init_p(void)
{
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;
	/*KER_PMI_ActScript_t stAct;*/

	I2C_DBG_LOG("_ker_iic_pm_init_p Start\n");
	pstMetaData = _ker_iic_load_p();
	if (!pstMetaData) {
		goto END_INIT;
	}

	pstMetaData->iInSuspend = FALSE;
	pstMetaData->iInComplete = FALSE;
	pstMetaData->iInPrepare = FALSE;
	pstMetaData->iPreInit = FALSE;
	pstMetaData->iRunSuspend = FALSE;

	/*stAct.resume = _ker_iic_resume_p;*/
	/*stAct.suspend = _ker_iic_suspend_p;*/
	/*stAct.prepare = _ker_iic_prepare_p;*/

	sema_init( &pstMetaData->stSema, 1 );
	init_waitqueue_head( &(pstMetaData->stWaitQ) );
	/*KER_PMI_Install (	KER_PMI_CLASS_I2C,
						KER_PMI_CLASS_SYSTEM,
						&stAct,
						3,
						0,
						(void*)NULL);*/
	I2C_DBG_LOG("_ker_iic_pm_init_p End\n");
END_INIT:
	return;
}


/*End STR function*/

/* Parsing SubAddress and return */
static u32  __rtnFilterSubAddr(u8 *subAdrBuf,
	enum EN_KER_IIC_SUBADDR_MODE selectMode)
{
	u8 i, idx;
	u32 rtnSubAddr = 0;

	switch (selectMode) {
	case EN_KER_IIC_NO_SUBADDR:
			idx = 0;
		break;
	case EN_KER_IIC_SUBADDR_8BITS:
			idx = 1;
		break;
	case EN_KER_IIC_SUBADDR_16BITS:
			idx = 2;
		break;
	case EN_KER_IIC_SUBADDR_24BITS:
			idx = 3;
		break;
	case EN_KER_IIC_SUBADDR_32BITS:
			idx = 4;
		break;
	default:
			idx = 0;
		break;
	}

	for (i = 0; i < idx ; i++)
		rtnSubAddr |= subAdrBuf[i] << (idx-i-1)*8;


	I2C_DBG_LOG("rtnSubAddr = 0x%08x\n", rtnSubAddr);

	return rtnSubAddr;
}


/* Return  re-mapping bus clock */
static enum EN_KER_IIC_CLOCK __rtnFilterBusClock(u32 bus_clk_rate)
{
	enum EN_KER_IIC_CLOCK rtnBusClk = EN_KER_IIC_CLOCK_100K;

	switch (bus_clk_rate) {
	case 100:
			rtnBusClk = EN_KER_IIC_CLOCK_100K;
		break;
	case 200:
			rtnBusClk = EN_KER_IIC_CLOCK_200K;
		break;
	case 300:
			rtnBusClk = EN_KER_IIC_CLOCK_300K;
		break;
	case 400:
			rtnBusClk = EN_KER_IIC_CLOCK_400K;
		break;
	default:
			rtnBusClk = EN_KER_IIC_CLOCK_200K;
		break;
	}
	return rtnBusClk;
}

/* Return Bus  HW mapping __iomem  address*/
static void __iomem *__rtnBaseAddr(int masterId)
{
	switch (masterId) {
	case 0:
			return reg0;
		break;
	case 1:
			return reg1;
		break;
	case 2:
			return reg2;
		break;
	case 3:
			return reg3;
		break;
	case 4:
			return reg4;
		break;
	case 5:
			return reg5;
		break;
	default:
			return reg0;
		break;
	}
}

/* Low-level register write function */
static inline void nt726xx_i2c_wr_reg(int masterId, int reg, u32 val)
{
	writel(val, __rtnBaseAddr(masterId) + reg);
}

/* Low-level register read function */
static inline u32 nt726xx_i2c_rd_reg(int masterId, int reg)
{
	return readl(__rtnBaseAddr(masterId) + reg);
}

/* Clear the specified interrupt */
static inline void nt726xx_i2c_clear_int(struct nt726xx_i2c_dev *dev, u32 msk)
{
	/*8nt726xx_i2c_wr_reg(dev, I2C_ICR, msk);*/
}

/* Enable the controller */
static inline void nt726xx_i2c_enable(struct nt726xx_i2c_dev *dev)
{
	/*u32 cr = nt726xx_i2c_rd_reg(dev, I2C_CR);*/
	/*cr |= I2C_CR_PE;*/
	/*nt726xx_i2c_wr_reg(dev, I2C_CR, cr);*/
}

/* Disable the controller */
static inline void nt726xx_i2c_disable(struct nt726xx_i2c_dev *dev)
{
	/*u32 cr = nt726xx_i2c_rd_reg(dev, I2C_CR);*/
	/*cr &= ~I2C_CR_PE;*/
	/*nt726xx_i2c_wr_reg(dev, I2C_CR, cr);*/
}

/* I2C Bus Reset */
static void _busReset(int masterId)
{
	u32 regVal;

	regVal = nt726xx_i2c_rd_reg(masterId, _REG_CONTROLREG);
	I2C_DBG_LOG("[Trace]@@@@@@Before Reset, regVal =0x%08x\n",
		regVal);

	nt726xx_i2c_wr_reg(masterId, _REG_CONTROLREG, (regVal & (~BIT_2)));

	regVal = nt726xx_i2c_rd_reg(masterId, _REG_CONTROLREG);
	I2C_DBG_LOG("[Trace]@@@@@@@After Reset, regVal =0x%08x\n",
		regVal);
	nt726xx_i2c_wr_reg(masterId, _REG_CONTROLREG, (regVal | BIT_2));
	I2C_DBG_LOG("[Trace]@@@@@@After Enable, regVal =0x%08x\n",
		(u32) nt726xx_i2c_rd_reg(masterId, _REG_CONTROLREG));
}

/* Do bus init */
static bool ntiic_bus_init(struct platform_device *pdev,
		enum EN_KER_IIC_CLOCK bus_clk,
		enum EN_KER_IIC_DUTY_LEVEL bus_duty,
		enum EN_KER_IIC_ACK_CTRL enAckCtrl, u8 enAckCtrlCounter)
{
	bool ret = false;
	int clockvalue, clk_div;
	unsigned long  ulPllCpu/*, ulData=0*/;

	if (gu32hclk_iic == 0)
		gu32hclk_iic = hclk;

	ulPllCpu = gu32hclk_iic;

	/* I2C Clock Rate = System_Clock / DIV_CNT(12..1) */
	switch (bus_clk) {
	case EN_KER_IIC_CLOCK_100K:
			clk_div = 100;
			break;
	case EN_KER_IIC_CLOCK_200K:
			clk_div = 200;
			break;
	case EN_KER_IIC_CLOCK_300K:
			clk_div = 300;
			break;
	case EN_KER_IIC_CLOCK_330K:
			clk_div = 330;
			break;
	case EN_KER_IIC_CLOCK_400K:
			clk_div = 400;
			break;
	case EN_KER_IIC_CLOCK_500K:
			clk_div = 500;
			break;
	case EN_KER_IIC_CLOCK_600K:
			clk_div = 600;
			break;
	case EN_KER_IIC_CLOCK_700K:
			clk_div = 700;
			break;
	case EN_KER_IIC_CLOCK_800K:
			clk_div = 800;
			break;
	case EN_KER_IIC_CLOCK_90K:
			clk_div =  90;
			break;
	case EN_KER_IIC_CLOCK_80K:
			clk_div =  80;
			break;
	case EN_KER_IIC_CLOCK_70K:
			clk_div =  70;
			break;
	case EN_KER_IIC_CLOCK_60K:
			clk_div =  60;
			break;
	case EN_KER_IIC_CLOCK_50K:
			clk_div =  50;
			break;
	case EN_KER_IIC_CLOCK_40K:
			clk_div =  40;
			break;
	case EN_KER_IIC_CLOCK_30K:
			clk_div =  30;
			break;
	case EN_KER_IIC_CLOCK_20K:
			clk_div =  20;
			break;
	case EN_KER_IIC_CLOCK_3K:
			clk_div =   3;
			break;
	default:
			clk_div = 100;
			break;
	};


	clockvalue = (((ulPllCpu + clk_div / 2) / clk_div) + 500) / 1000;

	nt726xx_i2c_wr_reg(gChannel, _REG_CLOCK, (clockvalue<<1) | (0<<16));

	FLUSH_REG(nt726xx_i2c_rd_reg(gChannel, _REG_CLOCK));

	I2C_DBG_LOG("@@@@@@before 0xfd000xc8=0x%x\n",
		nt726xx_i2c_rd_reg(gChannel, _REG_ACKCTL));
	nt726xx_i2c_wr_reg(gChannel, _REG_ACKCTL,
		(enAckCtrl << 2) | (enAckCtrlCounter<<3));
	I2C_DBG_LOG("@@@@@@after 0xfd000xc8=0x%x\n",
		nt726xx_i2c_rd_reg(gChannel, _REG_ACKCTL));
	FLUSH_REG(nt726xx_i2c_rd_reg(gChannel, _REG_ACKCTL));

	ret = true;
	return ret;
}

/* Set bus clock */
static bool ntiic_set_clk(enum EN_KER_IIC_CLOCK bus_clk)
{
	bool ret = false;
	int clockvalue;
	int clk_div;
	unsigned long  ulPllCpu;
	u32 duty;

	if (gu32hclk_iic == 0)
		gu32hclk_iic = hclk;

	ulPllCpu = gu32hclk_iic;

	/* I2C Clock Rate = System_Clock / DIV_CNT(12..1) */
	switch (bus_clk) {
	case EN_KER_IIC_CLOCK_100K:
			clk_div = 100;
			break;
	case EN_KER_IIC_CLOCK_200K:
			clk_div = 200;
			break;
	case EN_KER_IIC_CLOCK_300K:
			clk_div = 300;
			break;
	case EN_KER_IIC_CLOCK_330K:
			clk_div = 330;
			break;
	case EN_KER_IIC_CLOCK_400K:
			clk_div = 400;
			break;
	case EN_KER_IIC_CLOCK_500K:
			clk_div = 500;
			break;
	case EN_KER_IIC_CLOCK_600K:
			clk_div = 600;
			break;
	case EN_KER_IIC_CLOCK_700K:
			clk_div = 700;
			break;
	case EN_KER_IIC_CLOCK_800K:
			clk_div = 800;
			break;
	case EN_KER_IIC_CLOCK_90K:
			clk_div =  90;
			break;
	case EN_KER_IIC_CLOCK_80K:
			clk_div =  80;
			break;
	case EN_KER_IIC_CLOCK_70K:
			clk_div =  70;
			break;
	case EN_KER_IIC_CLOCK_60K:
			clk_div =  60;
			break;
	case EN_KER_IIC_CLOCK_50K:
			clk_div =  50;
			break;
	case EN_KER_IIC_CLOCK_40K:
			clk_div =  40;
			break;
	case EN_KER_IIC_CLOCK_30K:
			clk_div =  30;
			break;
	case EN_KER_IIC_CLOCK_20K:
			clk_div =  20;
			break;
	case EN_KER_IIC_CLOCK_3K:
			clk_div =   3;
			break;
	default:
			clk_div = 100;
			break;
	};


	clockvalue = (((ulPllCpu + clk_div/2) / clk_div) + 500) / 1000;
	/*clock stretching timeout =  no limitation */
	nt726xx_i2c_wr_reg(gChannel,
			_REG_CLOCK, (clockvalue<<1) |
			(0<<16));
	FLUSH_REG(nt726xx_i2c_rd_reg(gChannel, _REG_CLOCK));

	duty = (clockvalue*9 + 10)/20;

	nt726xx_i2c_wr_reg(gChannel, _REG_CLKDUTY, (duty<<16));
	FLUSH_REG(nt726xx_i2c_rd_reg(gChannel, _REG_CLKDUTY));
	ret = true;
	return ret;
}

/* Return timeout according to data size */
static u32 ntiic_count_timeout(struct i2c_adapter *adap,
		enum EN_KER_IIC_CLOCK bus_clk,
		u32 u32NumOfDataBytes)
{
	u32 uTimeout;

	/* timeout in second =  u32NumOfDataBytes * 10 (8) / bus clock
	=> for timeout margent*/
	switch (bus_clk) {
	case EN_KER_IIC_CLOCK_100K:
			uTimeout = (u32NumOfDataBytes * 10 / (100 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_200K:
			uTimeout = (u32NumOfDataBytes * 10 / (200 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_300K:
			uTimeout = (u32NumOfDataBytes * 10 / (300 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_330K:
			uTimeout = (u32NumOfDataBytes * 10 / (330 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_400K:
			uTimeout = (u32NumOfDataBytes * 10 / (400 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_500K:
			uTimeout = (u32NumOfDataBytes * 10 / (500 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_600K:
			uTimeout = (u32NumOfDataBytes * 10 / (600 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_700K:
			uTimeout = (u32NumOfDataBytes * 10 / (700 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_800K:
			uTimeout = (u32NumOfDataBytes * 10 / (800 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_90K:
			uTimeout = (u32NumOfDataBytes * 10 / (90 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_80K:
			uTimeout = (u32NumOfDataBytes * 10 / (80 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_70K:
			uTimeout = (u32NumOfDataBytes * 10 / (70 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_60K:
			uTimeout = (u32NumOfDataBytes * 10 / (60 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_50K:
			uTimeout = (u32NumOfDataBytes * 10 / (50 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_40K:
			uTimeout = (u32NumOfDataBytes * 10 / (40 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_30K:
			uTimeout = (u32NumOfDataBytes * 10 / (30 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_20K:
			uTimeout = (u32NumOfDataBytes * 10 / (20 * 1000)) + 1;
			break;
	case EN_KER_IIC_CLOCK_3K:
			uTimeout = (u32NumOfDataBytes * 10 / (3 * 1000)) + 1;
			break;
	default:/*CLOCK unknown*/
			uTimeout = 3;
			break;
	};

	I2C_DBG_LOG(" Maximum waiting time after event trigger: %d\n",
			uTimeout);
	if (uTimeout < 3)
		uTimeout = 3;
	return uTimeout;
}

/* Rx and Tx FIFO flushing */
static inline short nt726xx_i2c_fifo_flush(struct nt726xx_i2c_dev *dev)
{
#if 0
	u32 cr;
	int loop_cntr = 1000;

	/* Start flushing Rx and Tx FIFOs */
	/*cr = nt726xx_i2c_rd_reg(dev, I2C_CR);*/
	cr |= I2C_CR_FRX | I2C_CR_FTX;
	/*nt726xx_i2c_wr_reg(dev, I2C_CR, cr);*/

	/* Wait for completion */
	do {
		udelay(10);
		loop_cntr--;
	} while (nt726xx_i2c_rd_reg(dev, I2C_CR) & (I2C_CR_FRX | I2C_CR_FTX) &&
		 loop_cntr > 0);

	/* Check exit status */
	if (loop_cntr <= 0) {
		dev_warn(dev->dev, "Timeout waiting for fifo flushing\n");
		return -ETIMEDOUT;
	}
#endif
	return 0;
}


/* I2C controller initialization */
static int nt726xx_i2c_hwinit(struct platform_device *pdev, int channel)
{
	int  ret = 1;
	/*void __iomem *regTmp;*/

	/*
	for(i=0; i < NUM_OF_I2C_MASTER; i++){
		sema_init( &g_iic_sem_HW[i], 1 );
	}
	*/
	reg0 = ioremap_nocache(_BADDR_MASTER0, 0x100);
	reg1 = reg0 + _BADDR_MASTER1 - _BADDR_MASTER0;
	reg2 = reg0 + _BADDR_MASTER2 - _BADDR_MASTER0;
	reg3 = reg0 + _BADDR_MASTER3 - _BADDR_MASTER0;
	reg4 = reg0 + _BADDR_MASTER4 - _BADDR_MASTER0;
	reg5 = reg0 + _BADDR_MASTER5 - _BADDR_MASTER0;

	if (reg0 == NULL || reg1 == NULL || reg2 == NULL ||
		reg3 == NULL || reg4 == NULL || reg5 == NULL) {
		I2C_INF_LOG("[i2c fail][%s] reg0 ~ reg5 ioremap failed\n",
			pdev->name);
		return 0;
	}

	/* For SS 72658 setting*/
	/*regTmp = ioremap_nocache(0xFD0F0020, 1);

	if (regTmp == NULL) {
		I2C_INF_LOG("[%s] regTmp ioremap failed\n", pdev->name);
		return 0;
	}*/

	/*set-up GPA0/GPA1 to function pin //MA-Channel */
	/*writel(readl(regTmp) | BIT_2 | BIT_6, regTmp);*/

	/*set-up GPA2/GPA3 to function pin //MB-Channel */
	/*writel(readl(regTmp) | BIT_10 | BIT_14, regTmp);*/


	/*set-up GPA4/GPA5 to function pin //MC-Channel */
	/*writel(readl(regTmp) | BIT_18 | BIT_22, regTmp);*/

	/*set-up GPC16/GPC15 to function pin //MD-Channel*/
	/*GPC16 --> baseAdr =0xfd100028 = 0x00000001*/
	/*GPC15 --> baseAdr =0xfd100024 = 0x10000000*/
	/*regTmp = ioremap_nocache(0xfd100024, 1);
	if (regTmp == NULL) {
		I2C_INF_LOG("[%s] regTmp ioremap failed\n", pdev->name);
		return 0;
	}
	writel(readl(regTmp) | BIT_28, regTmp);

	regTmp = ioremap_nocache(0xfd100028, 1);
	if (regTmp == NULL) {
		I2C_INF_LOG("[%s] regTmp ioremap failed\n", pdev->name);
		return 0;
	}
	writel(readl(regTmp) | BIT_0, regTmp);*/

	/*set-up GPC18/GPC17 to function pin //ME-Channel */
	/*writel(readl(regTmp) | BIT_4 | BIT_8, regTmp);*/

	/*set-up GPC20/GPC19 to function pin //MF-Channel */
	/*writel(readl(regTmp) | BIT_12 | BIT_16, regTmp);*/

	I2C_DBG_LOG("pdev->name = %s\n", pdev->name);

	ret = ntiic_bus_init(pdev, EN_KER_IIC_CLOCK_200K,
			EN_KER_IIC_DUTY_LEVEL_0,
			EN_KER_IIC_ACK_CTRL_DISABLE,
			ackcontrolcounter[2]);
	if (!ret) {
		I2C_INF_LOG("[i2c fail][%s] can not init IIC bus-B\n",
			pdev->name);
		return 0;
	}

	ret = ntiic_set_clk(EN_KER_IIC_CLOCK_200K);
	if (!ret) {
		I2C_INF_LOG("[i2c fail][%s] set clk fail\n", pdev->name);
		return 0;
	}

	/*make sure HW init only do once*/
	gHw_init = TRUE;

	return ret;
}


/* Wait for bus release */
static short nt726xx_i2c_wait_bus_release(struct nt726xx_i2c_dev *dev)
{
#if 0
	unsigned long timeout;

	timeout = jiffies + I2C_TIMEOUT;
	while (nt726xx_i2c_rd_reg(dev, I2C_SR) & I2C_SR_STATUS_ONGOING) {
		if (time_after(jiffies, timeout)) {
			dev_warn(dev->dev, "Timeout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		usleep_range(1000, 2000);/*msleep(1);*/
	}
#endif

	return 0;
}

/* return FIFO 1/2  &  reg offset*/
static int rtnFIFO_Reg(u32 fifoIdx, u32 offset)
{
	if (fifoIdx == NT726xx_FIFO_1) {
		switch (offset) {
		case 0:
				return _REG_DATAFIFO1_1;
			break;
		case 1:
				return _REG_DATAFIFO1_2;
			break;
		case 2:
				return _REG_DATAFIFO1_3;
			break;
		case 3:
				return _REG_DATAFIFO1_4;
			break;
		default:
				return _REG_DATAFIFO1_1;
			break;
		}
	} else {/*NT726xx_FIFO_2*/
		switch (offset) {
		case 0:
				return _REG_DataFIFO2_1;
			break;
		case 1:
				return _REG_DataFIFO2_2;
			break;
		case 2:
				return _REG_DataFIFO2_3;
			break;
		case 3:
				return _REG_DATAFIFO2_4;
			break;
		default:
				return _REG_DataFIFO2_1;
			break;
		}
	}

}

/* ISR */
static irqreturn_t nt726xx_i2c_isr(int irq, void *dev_info)
{
	struct ST_I2C_INT_MAILBOX *i2c = dev_info;
	unsigned int temp, j, k, count;
	unsigned char *pu8Buffer;
	u32    tempBuffer;

	temp = (nt726xx_i2c_rd_reg(gChannel, _REG_INTCTRL) & 0x001F00);
	I2C_DBG_LOG("temp = 0x%08x\n", temp);
	if (temp != 0) {
		if ((temp & BIT_10) != 0) {/*RX full*/
			if (i2c[gChannel].NumOfBytes >= 16) {
				i2c[gChannel].NumOfBytes -= 16;
				count = 16;
			} else {
				count = i2c[gChannel].NumOfBytes;
				i2c[gChannel].NumOfBytes = 0;
			}
			pu8Buffer = i2c[gChannel].wPtr;

			if (i2c[gChannel].PingPongIndex == 0) {
				for (j = 0; j < 4; j++) {
					tempBuffer = nt726xx_i2c_rd_reg(
					gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, j));

					for (k = 0; k < 4; k++) {
						*pu8Buffer++ = tempBuffer &
						0xFF;
						tempBuffer >>= 8;
					}
				}
				nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_7);
				i2c[gChannel].PingPongIndex = 1;
			} else {
				for (j = 0; j < 4; j++) {
					tempBuffer = nt726xx_i2c_rd_reg(
					gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_2, j));

					for (k = 0; k < 4; k++)	{
						*pu8Buffer++ = tempBuffer &
						0xFF;
						tempBuffer >>= 8;
					}
				}

				nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_23);
				i2c[gChannel].PingPongIndex = 0;
			}
			i2c[gChannel].wPtr = pu8Buffer;
		} else if ((temp & BIT_9) != 0) {/*TX empty*/
			if (i2c[gChannel].NumOfBytes >= 16) {
				i2c[gChannel].NumOfBytes -= 16;
				count = 16;
			} else {
				count = i2c[gChannel].NumOfBytes;
				i2c[gChannel].NumOfBytes = 0;
			}
			pu8Buffer = i2c[gChannel].wPtr;
			if (i2c[gChannel].PingPongIndex == 0) {
				for (j = 0; j < count/4; j++) {
					tempBuffer = 0;
					for (k = 0; k < 4; k++)
						tempBuffer |= (*pu8Buffer++ <<
						(k*8));

					nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, j),
					tempBuffer);
				}
				if (count%4 != 0) {
					tempBuffer = 0;
					for (k = 0; k < count%4; k++)
						tempBuffer |= (*pu8Buffer++ <<
						(k*8));

					nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, j),
					tempBuffer);
				}
				nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_7);
				i2c[gChannel].PingPongIndex = 1;
			} else {
				for (j = 0; j < count/4; j++) {
					tempBuffer = 0;
					for (k = 0; k < 4; k++)
						tempBuffer |= (*pu8Buffer++ <<
						(k*8));

					nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_2, j),
					tempBuffer);
				}
				if (count%4 != 0) {
					tempBuffer = 0;
					for (k = 0; k < count%4; k++)
						tempBuffer |= (*pu8Buffer++ <<
						(k*8));

					nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_2, j),
					tempBuffer);
				}
				nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_23);
				i2c[gChannel].PingPongIndex = 0;
			}
			i2c[gChannel].wPtr = pu8Buffer;
		}
		/*clear interrupt*/
		nt726xx_i2c_wr_reg(gChannel,
		_REG_INTCTRL,
		nt726xx_i2c_rd_reg(gChannel, _REG_INTCTRL) |
		(temp << 8));
		g_iic_irq_num = g_iic_irq_num + 1;/*20170118 Test IRQ*/
		/*Don't need reply TX empty status to ntiic_write()*/
		i2c[gChannel].irq_flag = (temp & ~(BIT_9|BIT_10));
		i2c[gChannel].msg_num = 0;
		wake_up(&gstHWI2C.wait);

	}


	{
		struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

		pstMetaData = _ker_iic_load_p();
		if (pstMetaData && pstMetaData->iInSuspend) {
			disable_irq_nosync(gstHWI2C.irq_num);
			pstMetaData->iInComplete = TRUE;
			wake_up_interruptible(&pstMetaData->stWaitQ);
		}
	}


	return IRQ_HANDLED;
}

/* I2C master read */
static int nt726xx_i2c_xfer_rd(struct nt726xx_i2c_dev *i2c_dev,
	struct i2c_adapter *adap, struct i2c_msg *pmsg,
	struct ST_KEEP_MSGINFO stKpt, bool stop)
{
	struct nt726xx_i2c_dev *dev = i2c_get_adapdata(adap);
	enum EN_KER_IIC_SUBADDR_MODE selectMode = stKpt.stSubAdrMode;
	unsigned long	timeout;
	unsigned int	u32CtrlMask;
	wait_queue_head_t *waitQueue;
	u32 tempBuffer = 0;
	u32 u32SubAddress = stKpt.stSubAdr;
	u16 byteNumber = pmsg->len;
	u32 PingPongIndex = 0;
	u8 *pu8Data = pmsg->buf;
	u32 i, j;

	I2C_DBG_LOG(
	"[R]slave = 0x%02x, subM= %d, SubAd = %d, byteNum = %d, clk = %d\n",
	(pmsg->addr << 1), selectMode,
	u32SubAddress, byteNumber, i2c_dev->bus_clk_rate);

	if (byteNumber == 0) {
		up(&g_iic_sem_HW[adap->nr]);
		return -EINVAL;
	}


	/*down(&g_iic_sem_HW[adap->nr]);*/

	/*set global channel*/
	gChannel = adap->nr;
	I2C_DBG_LOG("gChannel = adap->nr  = %d\n", gChannel);

	gstI2CIntMailBox[gChannel].irq_flag = 0;

	waitQueue = &gstHWI2C.wait;

	/*bit2: I2C Enable*/
	nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG, BIT_2);

	/*clear FIFO data*/
	for (i = 0; i < 4; i++) {
		nt726xx_i2c_wr_reg(gChannel,
		rtnFIFO_Reg(NT726xx_FIFO_1, i), 0);
		nt726xx_i2c_wr_reg(gChannel,
		rtnFIFO_Reg(NT726xx_FIFO_2, i), 0);
	}

	ntiic_set_clk(__rtnFilterBusClock(i2c_dev->bus_clk_rate));

	I2C_DBG_LOG("u32SubAddress = 0x%04x, pmsg->len = %d\n",
	u32SubAddress, pmsg->len);

	switch (selectMode) {
	case EN_KER_IIC_NO_SUBADDR:
	{
			nt726xx_i2c_wr_reg(gChannel,
				_REG_SBADDR, 0);
			nt726xx_i2c_wr_reg(gChannel,
				_REG_CONTROLREG,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) &
				(~_I2C_CTRL_SUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_8BITS:
	{
			nt726xx_i2c_wr_reg(gChannel,
				_REG_SBADDR, u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel,
			_REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			(_I2C_CTRL_SUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_16BITS:
	{
			nt726xx_i2c_wr_reg(gChannel,
				_REG_SBADDR, u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel,
				_REG_CONTROLREG,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) |
				(_I2C_CTRL_SUBADDR_ENABLE |
				_I2C_CTRL_16BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_24BITS:
	{
			nt726xx_i2c_wr_reg(gChannel,
				_REG_SBADDR, u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel,
				_REG_CONTROLREG,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) |
				(_I2C_CTRL_SUBADDR_ENABLE |
				_I2C_CTRL_24BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_32BITS:
	{
			nt726xx_i2c_wr_reg(gChannel,
				_REG_SBADDR, u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel,
				_REG_CONTROLREG,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) |
				(_I2C_CTRL_SUBADDR_ENABLE |
				_I2C_CTRL_32BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_TOTAL:
	{
			break;
	}
	};

	/*bit count*/
	nt726xx_i2c_wr_reg(gChannel, _REG_SIZE, ((byteNumber) * 8)<<8);
	/*set "byte alignment*/
	nt726xx_i2c_wr_reg(gChannel, _REG_PINGPONGCTRL,
	nt726xx_i2c_rd_reg(gChannel, _REG_PINGPONGCTRL) |
	(BIT_7 | BIT_23 | BIT_13));

	/*(pmsg->addr << 1)  shift salve address from
	7-bit to 8-bits and enable read/write bit.*/
	u32CtrlMask = (((pmsg->addr << 1)<<8) |
	_I2C_CTRL_I2C_ENABLE | _I2C_CTRL_REPEAT_ENABLE |
	_I2C_CTRL_READ_OPERATION | _I2C_CTRL_CLOCK_STRETCH_ENABLE |
	_I2C_CTRL_MASTER_CLK_STRETCH_ENABLE |
	_I2C_CTRL_CLOCK_DUTY_ENABLE);

	nt726xx_i2c_wr_reg(gChannel,
	_REG_CONTROLREG,
	nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
	u32CtrlMask);
	nt726xx_i2c_wr_reg(gChannel,
	_REG_INTCTRL,
	(BIT_0 | BIT_3 | BIT_4 | BIT_16 |
	BIT_17 | BIT_18 | BIT_19 | BIT_20));

	if (byteNumber > 32) {
		nt726xx_i2c_wr_reg(gChannel,
		_REG_INTCTRL,
		nt726xx_i2c_rd_reg(gChannel, _REG_INTCTRL) |
		BIT_2);
		gstI2CIntMailBox[gChannel].wPtr = pu8Data;
		gstI2CIntMailBox[gChannel].NumOfBytes = byteNumber;
		gstI2CIntMailBox[gChannel].PingPongIndex = 0;
	}



	/* trigger  */
	u32CtrlMask |= (_I2C_CTRL_TRIGGER);
	nt726xx_i2c_wr_reg(gChannel,
	_REG_CONTROLREG,
	nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
	u32CtrlMask);

	I2C_DBG_LOG("len = %d, p(pmsg->buf) = 0x%08x\n",
	byteNumber, (u32)pmsg->buf);

	if ((byteNumber) <= 16) {
		I2C_DBG_LOG("[pmsg->len <= 16]\n");
		timeout = wait_event_timeout(*waitQueue,
		(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0, HZ * 3);
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) &
				BIT_24) != 0) {
				I2C_INF_LOG(
					"[i2c fail][i2c-%d][s=0x%02x] Dev NACK\n",
					adap->nr, (pmsg->addr << 1));
				up(&g_iic_sem_HW[adap->nr]);
				return -1;/*Device NACK*/
			}

			for (i = 0; i < (byteNumber)/4; i++) {
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_1, i));
				I2C_DBG_LOG("tempBuffer = 0x%08x\n",
				tempBuffer);
			#if 1
				for (j = 0; j < 4; j++) {
					I2C_DBG_LOG(
					"tempBuffer[%d] = 0x%08x\n",
					j, tempBuffer & 0xFF);
					*pu8Data++ = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
			#else
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_1, i));
				*pu32Data = tempBuffer;
				pu32Data++;
			#endif
			}

			if ((byteNumber)%4 != 0) {
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_1, i));
				for (j = 0; j < (byteNumber)%4; j++) {
					*pu8Data++ = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
			}
			/*Clear FIFO1*/
			nt726xx_i2c_wr_reg(gChannel,
			_REG_PINGPONGCTRL,
			nt726xx_i2c_rd_reg(gChannel,
			_REG_PINGPONGCTRL) |
			BIT_7);

			up(&g_iic_sem_HW[adap->nr]);
			return 0;		/*return OK*/
		} else {
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	} else if (byteNumber <= 32) {
		I2C_DBG_LOG("[pmsg->len <= 32]\n");
		timeout = wait_event_timeout(*waitQueue,
			(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0,
			HZ * 6);
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) &
				BIT_24) != 0) {
				I2C_INF_LOG("[i2c fail][i2c-%d] Dev NACK\n",
				adap->nr);
				up(&g_iic_sem_HW[adap->nr]);

				return -1;/*evice NACK*/
			}
			for (i = 0; i < 4; i++) {
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_1, i));
				#if 0
				for (j = 0; j < 4; j++) {
					*(++pmsg->buf) = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
				#endif

				for (j = 0; j < 4; j++) {
					I2C_DBG_LOG(
					"tempBuffer[%d] = 0x%08x\n",
					j, tempBuffer & 0xFF);
					*pu8Data++ = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
			}
			/*Clear FIFO1*/
			nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_7);
			(byteNumber) -= 16;
			for (i = 0; i < (byteNumber)/4; i++) {
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_2, i));

				for (j = 0; j < 4; j++) {
					I2C_DBG_LOG(
					"tempBuffer[%d] = 0x%08x\n",
					j, tempBuffer & 0xFF);
					*pu8Data++ = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
			}
			if ((byteNumber)%4 != 0) {
				tempBuffer = nt726xx_i2c_rd_reg(
				gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_2, i));

				for (j = 0; j < (byteNumber)%4; j++) {
					*pu8Data++ = tempBuffer &
					0xFF;
					tempBuffer >>= 8;
				}
			}
			/*Clear FIFO2*/
			nt726xx_i2c_wr_reg(gChannel,
				_REG_PINGPONGCTRL,
				nt726xx_i2c_rd_reg(gChannel,
				_REG_PINGPONGCTRL) |
				BIT_23);
			up(&g_iic_sem_HW[adap->nr]);

			return 0;/*return OK*/

		} else {
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	} else {/*pmsg->len  > 32*/
		I2C_DBG_LOG("[pmsg->len  > 32]\n");
		PingPongIndex = (((byteNumber)-1)/16)%2;
		timeout = wait_event_timeout(*waitQueue,
			(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0,
			HZ * ntiic_count_timeout(adap,
			__rtnFilterBusClock(i2c_dev->bus_clk_rate),
			byteNumber));
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel,
				_REG_CONTROLREG) &
				BIT_24) != 0) {
				I2C_INF_LOG("[i2c fail][i2c-%d] Dev NACK\n",
				adap->nr);
				up(&g_iic_sem_HW[adap->nr]);
				return -1;/*Device NACK*/
			}
			if (gstI2CIntMailBox[gChannel].NumOfBytes != 0) {
				pu8Data = gstI2CIntMailBox[gChannel].wPtr;
			if (gstI2CIntMailBox[gChannel].PingPongIndex == 0)
				j = NT726xx_FIFO_1;
			else
				j = NT726xx_FIFO_2;

				for (i = 0;
				i < gstI2CIntMailBox[gChannel].NumOfBytes;
				i++)
					*pu8Data++ =
						(nt726xx_i2c_rd_reg(
						gChannel,
						rtnFIFO_Reg(j,
						(i/4))) >> (8*(i%4)))&
						0xFF;

				nt726xx_i2c_wr_reg(gChannel,
					_REG_PINGPONGCTRL,
					nt726xx_i2c_rd_reg(gChannel,
					_REG_PINGPONGCTRL) |
					(BIT_7|BIT_23));
			}
			up(&g_iic_sem_HW[adap->nr]);

			return 0;		/*return OK*/

		} else {
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	}
	/* Initialize completion */
	/*init_completion(&dev->msg_complete);*/
	dev->msg = pmsg;
	dev->msg_err = 0;

	/* There is an error */
	/* TODO: take proper action on the controller */
	return -EIO;
}

/* I2C master write */
static int nt726xx_i2c_xfer_wr(struct nt726xx_i2c_dev *i2c_dev,
	struct i2c_adapter *adap, struct i2c_msg *pmsg,
	struct ST_KEEP_MSGINFO stKpt, bool stop)
{
	/*struct nt726xx_i2c_dev *dev = i2c_get_adapdata(adap);*/
	enum EN_KER_IIC_SUBADDR_MODE selectMode = stKpt.stSubAdrMode;
	unsigned long   timeout;
	unsigned int    u32CtrlMask;
	wait_queue_head_t *waitQueue;
	u32 tempBuffer = 0;
	u32 u32SubAddress = stKpt.stSubAdr;
	u16 byteNumber = pmsg->len;
	u8 *pu8Data = pmsg->buf;
	u32 i, k;

	if (byteNumber == 0) {
		up(&g_iic_sem_HW[adap->nr]);
		return -EINVAL;
	}

	I2C_DBG_LOG(
	"[W]slave=0x%02x, subMode=%d ,SubAddr=%d, byteNum=%d, clk=%d\n",
	(pmsg->addr << 1), selectMode,
	u32SubAddress, byteNumber,
	i2c_dev->bus_clk_rate);

	for (i = 0; i < byteNumber; i++)
		I2C_DBG_LOG(">>>111>>Check writed buffer wBuf[%d] = 0x%02x\n",
		i, pu8Data[i]);


	/*down(&g_iic_sem_HW[adap->nr]);*/

	/*set global channel*/
	gChannel = adap->nr;
	I2C_DBG_LOG("gChannel = adap->nr  = %d\n", gChannel);

	gstI2CIntMailBox[gChannel].irq_flag = 0;
	waitQueue = &gstHWI2C.wait;
	/*bit2: I2C Enable*/
	nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG, BIT_2);

	/*8clear FIFO data*/
	for (i = 0; i < 4; i++) {
		nt726xx_i2c_wr_reg(gChannel, rtnFIFO_Reg(NT726xx_FIFO_1, i), 0);
		nt726xx_i2c_wr_reg(gChannel, rtnFIFO_Reg(NT726xx_FIFO_2, i), 0);
	}

	I2C_DBG_LOG("u32SubAddress = 0x%04x, byteNumber = %d\n",
	u32SubAddress, byteNumber);

	ntiic_set_clk(__rtnFilterBusClock(i2c_dev->bus_clk_rate));

	switch (selectMode) {
	case EN_KER_IIC_NO_SUBADDR:
	{
			nt726xx_i2c_wr_reg(gChannel, _REG_SBADDR, 0);
			nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) &
			(~_I2C_CTRL_SUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_8BITS:
	{
			nt726xx_i2c_wr_reg(gChannel, _REG_SBADDR,
			u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			(_I2C_CTRL_SUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_16BITS:
	{
			nt726xx_i2c_wr_reg(gChannel, _REG_SBADDR,
			u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			(_I2C_CTRL_SUBADDR_ENABLE |
			_I2C_CTRL_16BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_24BITS:
	{
			nt726xx_i2c_wr_reg(gChannel, _REG_SBADDR,
			u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			(_I2C_CTRL_SUBADDR_ENABLE |
			_I2C_CTRL_24BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_32BITS:
	{
			nt726xx_i2c_wr_reg(gChannel, _REG_SBADDR,
			u32SubAddress);
			nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			(_I2C_CTRL_SUBADDR_ENABLE |
			_I2C_CTRL_32BITSUBADDR_ENABLE));
			break;
	}
	case EN_KER_IIC_SUBADDR_TOTAL:
	{
			break;
	}
	};
		/*bit count*/
		nt726xx_i2c_wr_reg(gChannel, _REG_SIZE,
				((byteNumber) * 8)<<8);
		/*set "byte alignment*/
		nt726xx_i2c_wr_reg(gChannel, _REG_PINGPONGCTRL,
		nt726xx_i2c_rd_reg(gChannel, _REG_PINGPONGCTRL) |
		(BIT_7 | BIT_23 | BIT_13));


		/*20150209, Eason , (pmsg->addr << 1)  shift salve address
		from 7-bit to 8-bits and enable read/write bit.*/
		u32CtrlMask = (((pmsg->addr << 1)<<8) |
		_I2C_CTRL_I2C_ENABLE |
		_I2C_CTRL_CLOCK_STRETCH_ENABLE |
		_I2C_CTRL_MASTER_CLK_STRETCH_ENABLE |
		_I2C_CTRL_CLOCK_DUTY_ENABLE);

		nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
		nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
		u32CtrlMask);
		nt726xx_i2c_wr_reg(gChannel, _REG_INTCTRL,
		(BIT_0 | BIT_3 | BIT_4 | BIT_16 |
		BIT_17 | BIT_18 | BIT_19 | BIT_20));
		if ((byteNumber) > 32) {
			/*enable tx empty INT.*/
			nt726xx_i2c_wr_reg(gChannel,
				_REG_INTCTRL,
				nt726xx_i2c_rd_reg(gChannel, _REG_INTCTRL) |
				 BIT_1);
		}

	if (byteNumber <= 16) {
		I2C_DBG_LOG("[byteNumber <= 16]\n");

		for (i = 0; i < byteNumber/4; i++) {
			tempBuffer = 0;
			for (k = 0; k < 4; k++)
				tempBuffer |= (*pu8Data++) << (k*8);

			I2C_DBG_LOG(">>>222>>  tempBuffer = 0x%08x\n",
					tempBuffer);
			nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, i),
					tempBuffer);
		}
		if (byteNumber%4 != 0) {
			tempBuffer = 0;
			for (k = 0; k < byteNumber%4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			I2C_DBG_LOG(">>>333>>  tempBuffer = 0x%08x\n",
					tempBuffer);
			nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, i),
					tempBuffer);
		}
		nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			_I2C_CTRL_TRIGGER);
		timeout = wait_event_timeout(*waitQueue,
			(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0,
			HZ * 3);
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) &
				BIT_24) != 0) {
				I2C_INF_LOG(
					"[i2c fail][i2c-%d][s=0x%02x] Dev NACK\n",
					adap->nr, (pmsg->addr << 1));

				up(&g_iic_sem_HW[adap->nr]);
				return -1;      /*Device NACK*/
			}
			I2C_DBG_LOG("Done!!\n");
			up(&g_iic_sem_HW[adap->nr]);
			return 0;       /*done*/

		} else {/*INTR_NOT_FINISH*/
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	} else if (byteNumber <= 32) {
		I2C_DBG_LOG("[byteNumber <= 32]\n");
		byteNumber -= 16;
		for (i = 0; i < 4; i++) {
			tempBuffer = 0;
			for (k = 0; k < 4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			I2C_DBG_LOG(">>>444>>  tempBuffer = 0x%08x\n",
					tempBuffer);
			nt726xx_i2c_wr_reg(gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_1, i),
				tempBuffer);
		}
		for (i = 0; i < byteNumber/4; i++) {
			tempBuffer = 0;
			for (k = 0; k < 4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			I2C_DBG_LOG(">>>555>>  tempBuffer = 0x%08x\n",
					tempBuffer);
			nt726xx_i2c_wr_reg(gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_2, i),
				tempBuffer);
		}
		if (byteNumber%4 != 0) {
			tempBuffer = 0;
			for (k = 0; k < byteNumber%4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			I2C_DBG_LOG(">>>666>>  tempBuffer = 0x%08x\n",
					tempBuffer);
			nt726xx_i2c_wr_reg(gChannel,
				rtnFIFO_Reg(NT726xx_FIFO_2, i),
				tempBuffer);
		}

		nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
			nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
			_I2C_CTRL_TRIGGER);

		timeout = wait_event_timeout(*waitQueue,
			(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0,
			HZ * 6);
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) &
				BIT_24) != 0) {
				I2C_INF_LOG("[i2c fail][i2c-%d] Dev NACK\n",
				adap->nr);

				up(&g_iic_sem_HW[adap->nr]);
				return -1;      /* Device NACK*/
			}
			up(&g_iic_sem_HW[adap->nr]);
			return 0;       /* done!!*/

		} else {/*INTR_NOT_FINISH*/
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	} else {/*byteNumber  > 32*/
		I2C_DBG_LOG("[i2c-%d][byteNumber  > 32]\n", adap->nr);
		for (i = 0; i < 4; i++) {
			tempBuffer = 0;
			for (k = 0; k < 4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_1, i),
					tempBuffer);
		}
		for (i = 0; i < 4; i++) {
			tempBuffer = 0;
			for (k = 0; k < 4; k++)
				tempBuffer |= ((*pu8Data++) << (k*8));

			nt726xx_i2c_wr_reg(gChannel,
					rtnFIFO_Reg(NT726xx_FIFO_2,
					i),
					tempBuffer);
		}

		byteNumber -= 32;
		gstI2CIntMailBox[gChannel].wPtr = pu8Data;
		gstI2CIntMailBox[gChannel].NumOfBytes = byteNumber;
		gstI2CIntMailBox[gChannel].PingPongIndex = 0;
		nt726xx_i2c_wr_reg(gChannel, _REG_CONTROLREG,
		nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) |
							_I2C_CTRL_TRIGGER);

		timeout = wait_event_timeout(*waitQueue,
		(gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0,
		HZ * ntiic_count_timeout(adap,
		__rtnFilterBusClock(i2c_dev->bus_clk_rate), byteNumber));
		/*INTR_FINISH*/
		if ((gstI2CIntMailBox[gChannel].irq_flag & BIT_8) != 0) {
			if ((nt726xx_i2c_rd_reg(gChannel, _REG_CONTROLREG) &
				BIT_24) != 0)	{
					I2C_INF_LOG(
					"[i2c fail][i2c-%d] Dev NACK\n",
					adap->nr);

				up(&g_iic_sem_HW[adap->nr]);
				return -1;/*Device NACK*/
			}
			up(&g_iic_sem_HW[adap->nr]);
			return 0;/*done!*/
		} else {/*INTR_NOT_FINISH*/
			I2C_INF_LOG(
			"[i2c fail][i2c-%d][s=0x%02x][0xe8=0x%08x][girq=0x%08x]Timeout\n",
			adap->nr, (pmsg->addr << 1),
			nt726xx_i2c_rd_reg(adap->nr, _REG_INTCTRL),
			gstI2CIntMailBox[gChannel].irq_flag);
			I2C_INF_LOG("[0xc0=0x%08x]",
				nt726xx_i2c_rd_reg(adap->nr, _REG_CONTROLREG));
			/*_i2c_reset_slaveDev(adap->nr);*/
			_busReset(adap->nr);
			up(&g_iic_sem_HW[adap->nr]);
			return -1;
		}
	}

	/* Initialize completion */
	/* There is an error */
	/* TODO: take proper action on the controller */
	return -EIO;
}

/* nt726xx Xfer function */
static int nt726xx_i2c1_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			int num)
{
	struct nt726xx_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	struct i2c_msg *pmsg;
	struct ST_KEEP_MSGINFO stKpt;

	int i, r;
	bool stop;
	/* Power management: increment device usage counter */
	pm_runtime_get_sync(i2c_dev->dev);

	/* Wait for bus release */
	/* FIXME: really necessary in single master mode? */
	r = nt726xx_i2c_wait_bus_release(i2c_get_adapdata(adap));
	if (r)
		goto out;

	down(&g_iic_sem);
	down(&g_iic_sem_HW[adap->nr]);

	if (num == 1) {/*no SubAddress, only read or write*/
		pmsg = &msgs[0];
		stop = (0 == (num - 1));
		stKpt.stSubAdrMode = EN_KER_IIC_NO_SUBADDR;
		stKpt.stSubAdr = 0;

		if (pmsg->flags & I2C_M_RD) {/*read*/
			r = nt726xx_i2c_xfer_rd(i2c_dev, adap,
									pmsg,
									stKpt,
									stop);
		} else {/*write*/
			r = nt726xx_i2c_xfer_wr(i2c_dev, adap,
									pmsg,
									stKpt,
									stop);
		}
	} else if (num == 2) {/*with SubAddress*/
		for (i = 0; i < num ; i++) {
			stop = (i == (num - 1));
			pmsg = &msgs[i];
			if (i == 0) {
				stKpt.stSubAdrMode =
					(enum EN_KER_IIC_SUBADDR_MODE)pmsg->len;
				stKpt.stSubAdr = __rtnFilterSubAddr(
					pmsg->buf,
					stKpt.stSubAdrMode);
			} else if (i == 1) {
				if (pmsg->flags & I2C_M_RD) {/*read*/
					r = nt726xx_i2c_xfer_rd(i2c_dev,
					adap, pmsg,
					stKpt, stop);
				} else {/*write*/
					r = nt726xx_i2c_xfer_wr(
					i2c_dev,
					adap, pmsg,
					stKpt, stop);
				}
			} else {
			}
		}
	} else {
		I2C_INF_LOG("[i2c fail]un-handled case. ");
	}
/*#if I2C_SUSPEND_ENABLE*/
	/*add STR transaction record*/
	{
		struct _ker_iic_cmd_rec_t stCmdParam;

		stCmdParam.enBus = adap->nr;
		stCmdParam.enClk = EN_KER_IIC_CLOCK_400K;
		stCmdParam.enSubAddrMode =
		EN_KER_IIC_NO_SUBADDR;
		stCmdParam.u8SlaveAddr = (pmsg->addr << 1);
		stCmdParam.u32SubAddr = stKpt.stSubAdr;
		stCmdParam.u32NumOfBytes = pmsg->len;
		stCmdParam.pu8Buf = pmsg->buf;

		_ker_iic_record_add_p((void *)&stCmdParam);
	}
/*#endif*/

	I2C_DBG_LOG("r = %d, num = %d\n", r, num);
	up(&g_iic_sem);


out:
	/* Power management: decrement device usage counter */
	pm_runtime_put(i2c_dev->dev);

	return r == 0 ? num : r;
}

/* I2C supported functionality */
static u32 nt726xx_i2c1_func(struct i2c_adapter *adap)
{
	/* TODO: add more functionality as they are implemented */
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct of_device_id i2cNt7x6xx_of_match[];

/*nt726xx i2c  module init */
static int _nt726xx_init_md(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct nt726xx_i2c_dev *i2c;
	struct i2c_adapter *adap;
	u32 bus_clock = 0;
	u32 u32_i2c_irq = 0;
	int i;

	int err = 0;
	/*20160711 move to kernel, it will use resume/
	suspend function of I2C standard.*/
	if (!IS_IIC_PM_INIT) {
		_ker_iic_pm_init_p();
		IS_IIC_PM_INIT = TRUE;
	}

	match = of_match_device(i2cNt7x6xx_of_match, &pdev->dev);
	if (!match) {
		I2C_INF_LOG("[i2c fail]failed to match device\n");
		return -EINVAL;
	}

	/* Allocate memory for the NT726xx I2C device data */
	g_i2c = kzalloc(sizeof(struct nt726xx_i2c_dev), GFP_KERNEL);
	i2c = g_i2c;

	if (!i2c) {
		I2C_INF_LOG(
			"[i2c fail]failed to allocate  nt726xx_i2c_dev memory\n");
		return -ENOMEM;
	}

	i2c->dev = &pdev->dev;

	i2c->base = of_iomap(pdev->dev.of_node, 0);
	if (!i2c->base) {
		I2C_INF_LOG("[i2c fail]failed to map controller\n");
		err = -ENOMEM;
		goto fail_map;
	}

	/*read DTS(interrupts)*/
	err = of_property_read_u32(pdev->dev.of_node,
	"interrupts", &u32_i2c_irq);
	I2C_DBG_LOG("read DTS irq = %d\n", u32_i2c_irq);
	if (err) {
		I2C_DBG_LOG("use default irq = %d\n", I2C_IRQ);
		i2c->irq = I2C_IRQ; /* default ieq */
	} else {
	/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))*/
		{
			struct device_node *np = NULL;

			np = of_find_node_by_name(NULL, "i2c");
			if (np == NULL)
				I2C_DBG_LOG("[0]err node not found-i2c\n");

			u32_i2c_irq = irq_of_parse_and_map(np, 0);
		}
	/*#endif*/
		i2c->irq = u32_i2c_irq;
		I2C_DBG_LOG("use dts irq = %d\n", u32_i2c_irq);
	}

	/*read DTS(clock-frequency)*/
	err = of_property_read_u32(pdev->dev.of_node,
	"clock-frequency", &bus_clock);
	if (err) {
		I2C_DBG_LOG("use default clock-frequency --> 100k\n");
		i2c->bus_clk_rate = 100; /* default clock rate */
	} else {
		i2c->bus_clk_rate = bus_clock;
		I2C_DBG_LOG("use dts clock-frequency = %d\n", bus_clock);
	}

	if (gbirq_init == FALSE) {
		for (i = 0; i < NUM_OF_I2C_MASTER; i++)
			sema_init(&g_iic_sem_HW[i], 1);

		sema_init(&g_iic_sem, 1);

		if (i2c->irq) { /* no i2c->irq implies polling */
			I2C_DBG_LOG("Using i2c->irq as irq = %d\n", i2c->irq);
/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))*/
		{
			struct device_node *np = NULL;
			u32		u32Irq;

			np = of_find_node_by_name(NULL, "i2c");
			if (np == NULL)
				I2C_DBG_LOG("[1]err node not found-i2c\n");

			u32Irq = irq_of_parse_and_map(np, 0);
			err    = request_irq(u32Irq, nt726xx_i2c_isr,
				     IRQF_SHARED | IRQF_TRIGGER_HIGH,
				     "i2c", &gstI2CIntMailBox);
		}
/*#else
			err = request_irq(i2c->irq, nt726xx_i2c_isr,
					     IRQF_SHARED | IRQF_TRIGGER_HIGH,
					     "irq-i2c-nt726xx",
					     &gstI2CIntMailBox);
#endif*/
			if (err < 0) {
				I2C_INF_LOG(
					"[i2c fail][%s] failed to attach interrupt\n",
					pdev->name);
				goto fail_request;
			}
		} else {
			I2C_DBG_LOG("Using I2C_IRQ as irq = %d\n",
					 i2c->irq);
/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))*/
		{
			struct device_node *np = NULL;
			u32		u32Irq;

			np = of_find_node_by_name(NULL, "i2c");
			if (np == NULL)
				I2C_DBG_LOG("[2]err node not found-i2c\n");

			u32Irq = irq_of_parse_and_map(np, 0);
			err    = request_irq(u32Irq, nt726xx_i2c_isr,
					IRQF_SHARED | IRQF_TRIGGER_HIGH,
					"i2c", &gstI2CIntMailBox);
		}
/*#else
			err = request_irq(I2C_IRQ, nt726xx_i2c_isr,
						IRQF_SHARED | IRQF_TRIGGER_HIGH,
						"irq-i2c-nt726xx",
						&gstI2CIntMailBox);
#endif*/
			if (err < 0) {
				I2C_INF_LOG(
					"[i2c fail][%s] failed to attach interrupt\n",
					pdev->name);
				goto fail_request;
			}
		}
		gbirq_init = TRUE;
	}

	/* Get IRQ */
	gstHWI2C.irq_num = i2c->irq;

	I2C_DBG_LOG("i2c->irq = %d\n", i2c->irq);

	platform_set_drvdata(pdev, i2c);

	g_adap = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	adap = g_adap;

	if (!adap) {
		I2C_INF_LOG(
			"[i2c fail][i2c-%d] failed to allocate adapter\n",
			pdev->id);
		err = -ENOMEM;
		goto fail_request;
	}

	/* Setup I2C adapter */
	adap->owner = THIS_MODULE;
	strlcpy(adap->name, "NT726xx I2C adapter", sizeof(adap->name));
	adap->algo = &nt726xx_i2c_algo;

	adap->class = I2C_CLASS_HWMON;
	adap->dev.parent = &pdev->dev;

	i2c->adapter = adap;
	i2c_set_adapdata(i2c->adapter, i2c);

	i2c->adapter->dev.parent = &pdev->dev;
	i2c->adapter->dev.of_node = of_node_get(pdev->dev.of_node);

	/*i2c_add_adapter will auto assign apdp->nr (0, 1, 2,....5)*/
	err = i2c_add_adapter(i2c->adapter);
	if (err < 0) {
		I2C_INF_LOG(
			"[i2c fail][%s] failed to add adapter\n", pdev->name);
		goto fail_add;
	}

/*#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	of_i2c_register_devices(i2c->adapter);
#endif*/

	gstHWI2C.irq_init = 1;
	I2C_DBG_LOG("adap->nr = %d\n", adap->nr);
	return 0;

fail_add:
	kfree(adap);
/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))*/
	{
		struct device_node *np;
		int irq_num;

		np = of_find_node_by_name(NULL, "i2c");
		if (np == NULL)
			I2C_DBG_LOG("[4]error node not found - i2c\n");

		irq_num = irq_of_parse_and_map(np, 0);

		free_irq(irq_num, i2c);
	}
/*#else
	free_irq(i2c->irq, i2c);
#endif*/

fail_request:
	   irq_dispose_mapping(i2c->irq);
	   iounmap(i2c->base);

fail_map:
	/*use async*/
	del_timer_sync(&IRQ_report_timer);/*20170118 Test IRQ*/
	kfree(i2c);
	return err;
}

/*nt726xx i2c  module remove */
static int _nt726xx_rm_md(struct platform_device *pdev)
{
	struct resource *mem;

/*#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))*/
	struct device_node *np;
	int irq_num;

	np = of_find_node_by_name(NULL, "i2c");
	if (np == NULL)
		I2C_DBG_LOG("[3]error node not found - i2c\n");

	irq_num = irq_of_parse_and_map(np, 0);

	free_irq(irq_num, g_i2c);
/*#else
	free_irq(g_i2c->irq, g_i2c);
#endif*/

	i2c_del_adapter(g_i2c->adapter);
	kfree(g_i2c->adapter);
	pm_runtime_disable(g_i2c->dev);
	iounmap(g_i2c->base);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (mem != NULL)
		release_mem_region(mem->start, resource_size(mem));

	platform_set_drvdata(pdev, NULL);
	/*use async*/
	del_timer_sync(&IRQ_report_timer);/*20170118 Test IRQ*/
	kfree(g_adap);
	kfree(g_i2c);

	return 0;
}

static void I2C_IRQ_report(unsigned long arg)
{
	int i;
	/*pr_crit("I2C_irq_num=0x%x\n", g_iic_irq_num);*/
	if (g_iic_irq_num > 100000) {
		pr_crit("I2C_irq_num=0x%x\n", g_iic_irq_num);
		for (i = 0; i < NUM_OF_I2C_MASTER; i++) {
			pr_crit("0x%dC0 0x%08x ", i,
				nt726xx_i2c_rd_reg(i, _REG_CONTROLREG));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_CLOCK));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_ACKCTL));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_SIZE));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DATAFIFO1_1));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DATAFIFO1_2));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DATAFIFO1_3));
			pr_crit("0x%08x\n",
				nt726xx_i2c_rd_reg(i, _REG_DATAFIFO1_4));
			pr_crit("0x%dE0 0x%08x ", i,
				nt726xx_i2c_rd_reg(i, _REG_SBADDR));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_PINGPONGCTRL));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_INTCTRL));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DataFIFO2_1));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DataFIFO2_2));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DataFIFO2_3));
			pr_crit("0x%08x ",
				nt726xx_i2c_rd_reg(i, _REG_DATAFIFO2_4));
			pr_crit("0x%08x\n",
				nt726xx_i2c_rd_reg(i, _REG_CLKDUTY));
		}
	} else if ((g_iic_irq_num > 6000) && (g_iic_irq_num < 10000)) {
		for (i = 0; i < NUM_OF_I2C_MASTER; i++) {
			pr_crit("0x%dC0 0x%08x ", i,
			nt726xx_i2c_rd_reg(i, _REG_CONTROLREG));
			pr_crit("0x%08x ",
			nt726xx_i2c_rd_reg(i, _REG_INTCTRL));
		}
	}

	g_iic_irq_num = 0;

	IRQ_report_timer.expires = jiffies + 1*HZ;
	add_timer(&IRQ_report_timer);
}

static void I2C_IRQ_init_timer(void)
{
	pr_crit("I2C IRQ init_timer start(0324)\n");
	g_iic_irq_num = 0;
	/* Timer init */
	init_timer(&IRQ_report_timer);
	/* define timer function */
	IRQ_report_timer.function = I2C_IRQ_report;
	/* define timer send Data */
	IRQ_report_timer.data = ((unsigned long) 0);
	/* define timer Delay time */
	IRQ_report_timer.expires = jiffies + HZ;
	/* define Timer*/
	add_timer(&IRQ_report_timer);
}

/*AMP Nreset control API start*/
struct micom_bt_amp_cmd {
	int length;
	unsigned char cmd;
	unsigned char ack;
	unsigned char data[10];
};

static struct micom_bt_amp_cmd mbtc[] = {
	{
		.length = 5,
		.cmd = 0x6a,
		.ack = 0x6a,
		.data[0] = 0x1,
		.data[1] = 0x0,
		.data[2] = 0x0,
		.data[3] = 0x0,
		.data[4] = 0x0,

	},
};

static unsigned long I2C_local_find_symbol(char *name)
{
	struct kernel_symbol *sym = NULL;

	sym = (void *)find_symbol(name, NULL, NULL, 1, true);
	if (sym)
		return sym->value;
	else
		return 0;
}

void I2C_local_send_micom(int cmd_type)
{
	unsigned char local_cmd, local_ack;
	char *local_data;
	int ret, len, retry;

	struct micom_bt_amp_cmd *mbtc_p = mbtc;

	static int (*micom_fn)(char cmd, char ack, char *data, int len);
	/* get func pointer */
	if (micom_fn == NULL) {
		micom_fn =
			(void *)I2C_local_find_symbol("sdp_micom_send_cmd_ack");
		if (!micom_fn) {
			I2C_INF_LOG(
				"[%s] can not find a symbol [sdp_micom_send_cmd_ack]\n",
				__func__);
		return;
		}
	}

	local_cmd = mbtc_p[0].cmd;
	local_data = (char *)mbtc_p[0].data;
	local_ack = mbtc_p[0].ack;
	len = mbtc_p[0].length;

	I2C_INF_LOG(
	"[%s] sdp_micom_send_cmd_ack cmd %u ack %u data %u %u %u %u %u len %d\n",
	__func__, local_cmd, local_ack,
	local_data[0], local_data[1], local_data[2],
	local_data[3], local_data[4], len);

	retry = 3;
	while (1) {
		ret = micom_fn(local_cmd, local_ack, local_data, len);
		if (!ret)
			break;

		I2C_INF_LOG("[%s]sdp_micom_send_cmd_ack fail %d retry %d\n",
			__func__, ret, retry);

		retry--;
		if (retry == 0) {
			I2C_INF_LOG("[%s]sdp_micom_send_cmd_ack give up\n",
				__func__);
			return;
		}
	}
	/*
		I2C_INF_LOG("[%s] sdp_micom_send_cmd_ack ok\n", __func__);
	*/
}
/*AMP Nreset control API end*/

static void _i2c_reset_slaveDev(int bus)
{
	I2C_INF_LOG("bus[%d] _i2c_reset_slaveDev\n", bus);
	switch (bus) {
	case EN_KER_IIC_BUS_3:/*Tuner nreset*/
		if (!nt726xx_gpio_request(NULL, Tuner_Nreset)) {
			nt726xx_gpio_set(NULL, Tuner_Nreset,
					GPIO_HPD_LOW);
			I2C_INF_LOG(
					"GPIO%d for tuner nreset write low OK\n",
					Tuner_Nreset);
			usleep_range(10000, 12000);
			nt726xx_gpio_set(NULL, Tuner_Nreset,
					GPIO_HPD_HIGH);
			I2C_INF_LOG(
					"GPIO%d for tuner nreset write high OK\n",
					Tuner_Nreset);
			nt726xx_gpio_free(NULL, Tuner_Nreset);
		} else {
			I2C_INF_LOG("GPIO-%d request fail.\n",
					Tuner_Nreset);
		}
		break;
	case EN_KER_IIC_BUS_4:/*AMP nreset*/
		I2C_local_send_micom(0);
		break;
	default:
		break;

	}
}

/* nt726xx i2c bus-0 platform_driver probe init */
static int nt726xx_i2c_probe(struct platform_device *pdev)
{
	int ret = 0, hwInitDone = 0;
	/*--------------------------------------------------------*/
	/*1.platform_driver_register( &nt726xx_i2c_driver )  first*/
	/*2.by match compatible property in DTS.*/
	/*3.i2cNt7x6xx_of_match table, then do nt726xx_i2c_probe()*/
	/*1.-->2.-->3.*/
	/*--------------------------------------------------------*/
	I2C_DBG_LOG("$$$$$$$$$$$$$ Probe...\n");
	I2C_DBG_LOG("pdev->name = %s\n", pdev->name);

	/*make sure HW init only do once*/
	if (gHw_init == FALSE) {
		I2C_IRQ_init_timer();
		hwInitDone = nt726xx_i2c_hwinit(pdev, 0);
		if (!hwInitDone) {
			I2C_DBG_LOG("[pdev->name = %s] HW init failed ...\n",
				 pdev->name);
		}
		memset(gstI2CIntMailBox, 0,
			sizeof(struct ST_I2C_INT_MAILBOX)*NUM_OF_I2C_MASTER);
	}

	ret = _nt726xx_init_md(pdev);
	if (ret != 0) {
		I2C_DBG_LOG("[pdev->name = %s] Probe failed ...\n",
			 pdev->name);

		/*Delete timer*/
		del_timer_sync(&IRQ_report_timer);/*20170118 Test IRQ*/
		return ret;
	}

	return ret;

}
/* nt726xx i2c bus-0 platform_driver remove  */
static int nt726xx_i2c_remove(struct platform_device *pdev)
{
	I2C_DBG_LOG("$$$$$$$$$$$$$ Exit...\n");
	_nt726xx_rm_md(pdev);
	return 0;
}

static const struct of_device_id i2cNt7x6xx_of_match[] = {
		{
			.compatible = "nvt,nt726xx_i2c.0",
		},
		{ .compatible = "nvt,nt726xx_i2c.1", },
		{ .compatible = "nvt,nt726xx_i2c.2", },
		{ .compatible = "nvt,nt726xx_i2c.3", },
		{ .compatible = "nvt,nt726xx_i2c.4", },
		{ .compatible = "nvt,nt726xx_i2c.5", },
		{ }
};

static struct platform_driver nt726xx_i2c_driver = {
	.driver		= {
		.name	= "nt726xx_i2c",/*DRIVER_NAME,*/
		.owner	= THIS_MODULE,
		.pm	= &nt726xx_i2c_pm_ops,
		.of_match_table = of_match_ptr(i2cNt7x6xx_of_match),
		},
	.probe		= nt726xx_i2c_probe,
	/*.remove		= __exit_p(nt726xx_i2c_remove),*/
	.remove		= nt726xx_i2c_remove,
};

/* Probe entry when system boot up  */
int __init i2c_nt726xx_platform_init(void)
{
	/*Just need to register common driver(for all) once*/
	I2C_DBG_LOG("nt726xx_i2c_driver register\n");
	return platform_driver_register(&nt726xx_i2c_driver);
}

/* Exit entry when system shut down  */
void __exit i2c_nt726xx_platform_exit(void)
{
	platform_driver_unregister(&nt726xx_i2c_driver);
}

postcore_initcall(i2c_nt726xx_platform_init);

module_exit(i2c_nt726xx_platform_exit);

#if I2C_SUSPEND_ENABLE
struct _ker_iic_convertParam_t {
	EN_KER_IIC_BUS				enChannel;
	EN_KER_IIC_SUBADDR_MODE	enSubAddrMode;
	unsigned long					ulRegAddr;
	EN_KER_IIC_CLOCK			enClk;

};
static void _kernel_i2c_convert_p(unsigned char u8Channel,
	unsigned char u8AddrSizeIIC, unsigned char *pu8AddrIIC,
	_ker_iic_convertParam_t *pstParam);
static EN_KER_IIC_CLOCK  _kernel_iic_clockmapping_p(u32 u32Frq);

int Suspend_IIC_Init(void)
{
	return TRUE;
}
EXPORT_SYMBOL(Suspend_IIC_Init);

int Suspend_IIC_Read(unsigned char u8IdIIC, unsigned short u16ClockIIC,
	unsigned char u8SlaveIdIIC,	unsigned char u8AddrSizeIIC,
	unsigned char *pu8AddrIIC, unsigned int u32BufSizeIIC,
	unsigned char *pu8BufIIC, unsigned char u8ModeIIC)
{
	/* If you need to convert parameter, Do It */
	int iRet = 0;
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

	_ker_iic_convertParam_t stParam;
	EN_KER_IIC_BUS stBusID;
	EN_KER_IIC_SUBADDR_MODE enSubAddrMode;
	EN_KER_IIC_CLOCK clk;
	u32 u32SubAddress;
	unsigned char *pucDataBytesBuf = pu8BufIIC;

	pstMetaData = _ker_iic_load_p();

	_ker_iic_lock_p();
	if (pstMetaData && pstMetaData->iPreInit == FALSE) {
		/*if (KER_PMI_InSuspend()) {
		} else */if (pstMetaData->iRunSuspend) {
			KER_PMI_Prepare(KER_PMI_CMD_RESUME);
			KER_PMI_FastRun();
			pstMetaData->iPreInit = TRUE;
			pstMetaData->iRunSuspend = FALSE;
		}
	}
	_ker_iic_unlock_p();

	if (!pucDataBytesBuf || !pu8AddrIIC) {
		iRet = -1;
		I2C_INF_LOG("[i2c fail]NULL DATA\n");
		goto END_READ;
	}

	_kernel_i2c_convert_p(u8IdIIC, u8AddrSizeIIC, pu8AddrIIC, &stParam);
	clk = stParam.enClk = _kernel_iic_clockmapping_p(u16ClockIIC);

	stBusID = (EN_KER_IIC_BUS)stParam.enChannel;
	enSubAddrMode = (EN_KER_IIC_SUBADDR_MODE)stParam.enSubAddrMode;
	u32SubAddress = (u32)stParam.ulRegAddr;

END_READ:
	return iRet;
}
EXPORT_SYMBOL(Suspend_IIC_Read);


int Suspend_IIC_Write(unsigned char u8IdIIC,
	unsigned short u16ClockIIC, unsigned char u8SlaveIdIIC,
	unsigned char u8AddrSizeIIC, unsigned char *pu8AddrIIC,
	unsigned int u32BufSizeIIC, unsigned char *pu8BufIIC)
{
	/* If you need to convert parameter, Do It */
	int iRet = 0;
	struct _ker_iic_pmi_ctrl_t *pstMetaData = NULL;

	_ker_iic_convertParam_t stParam;
	EN_KER_IIC_BUS stBusID;
	EN_KER_IIC_SUBADDR_MODE enSubAddrMode;
	EN_KER_IIC_CLOCK clk;
	u32 u32SubAddress;

	unsigned char *pucDataBytesBuf = pu8BufIIC;

	/* Temporary solution for STR, Owner need to
	implement resume function of BT amp */
	/* Now We just drop the command when uIdIIC that
	mean BT amp to avoid system hange after resume */
	/* -> */

	if (u8IdIIC == 2) {
		if ((u8SlaveIdIIC != 70) && (u8SlaveIdIIC != 68))
			return iRet;
	}

	if (u8IdIIC == 6)
		return iRet;

	/* <- 20131223*/

	pstMetaData = _ker_iic_load_p();

	_ker_iic_lock_p();
	if (pstMetaData && pstMetaData->iPreInit == FALSE) {
		/*if (KER_PMI_InSuspend()) {
		} else */if (pstMetaData->iRunSuspend) {
			KER_PMI_Prepare(KER_PMI_CMD_RESUME);
			KER_PMI_FastRun();
			pstMetaData->iPreInit = TRUE;
			pstMetaData->iRunSuspend = FALSE;
		}
	}
	_ker_iic_unlock_p();

	if (!pucDataBytesBuf || !pu8AddrIIC) {
		iRet = -1;
		I2C_INF_LOG("[i2c fail]NULL DATA\n");
		goto END_WRITE;
	}

	_kernel_i2c_convert_p(u8IdIIC, u8AddrSizeIIC, pu8AddrIIC, &stParam);
	clk = stParam.enClk = _kernel_iic_clockmapping_p(u16ClockIIC);

	stBusID = (EN_KER_IIC_BUS)stParam.enChannel;
	enSubAddrMode = (EN_KER_IIC_SUBADDR_MODE)stParam.enSubAddrMode;
	u32SubAddress = (u32)stParam.ulRegAddr;

	if (iRet != 0) {
		I2C_INF_LOG(
			"[i2c fail]LINE %d, FUN %s Id %d Clk %d SId %d AddrSize %d\n",
			__LINE__,
			__func__,
			u8IdIIC, u16ClockIIC, u8SlaveIdIIC, u8AddrSizeIIC);
	}

END_WRITE:
	return iRet;
}
EXPORT_SYMBOL(Suspend_IIC_Write);

static void _kernel_i2c_convert_p(unsigned char u8Channel,
			unsigned char u8AddrSizeIIC,
			unsigned char *pu8AddrIIC,
			_ker_iic_convertParam_t *pstParam)
{
	if (!pu8AddrIIC || !pstParam)
		return;

	/*channel setting*/
	switch (u8Channel) {
	case 0:
		pstParam->enChannel = EN_KER_IIC_BUS_0;
		break;
	case 1:
		pstParam->enChannel = EN_KER_IIC_BUS_1;
		break;
	case 2:
		pstParam->enChannel = EN_KER_IIC_BUS_2;
		break;
	case 3:
		pstParam->enChannel = EN_KER_IIC_BUS_3;
		break;
	case 4:
		pstParam->enChannel = EN_KER_IIC_BUS_4;
		break;
	case 5:
		pstParam->enChannel = EN_KER_IIC_BUS_5;
		break;
	case 6:
		pstParam->enChannel = EN_KER_IIC_BUS_6;
		break;
	default:
		break;
	}

	/*address mode setting*/
	switch (u8AddrSizeIIC) {
	case 0:
			pstParam->enSubAddrMode = EN_KER_IIC_NO_SUBADDR;
			pstParam->ulRegAddr = KER_IIC_FLAG_NO_ADDR;
			break;
	case 1:
			pstParam->enSubAddrMode = EN_KER_IIC_SUBADDR_8BITS;
			pstParam->ulRegAddr = pu8AddrIIC[0];
			break;
	case 2:
			pstParam->enSubAddrMode = EN_KER_IIC_SUBADDR_16BITS;
			pstParam->ulRegAddr = (pu8AddrIIC[0]<<8) |
			(pu8AddrIIC[1]);
			break;
	case 3:
			pstParam->enSubAddrMode = EN_KER_IIC_SUBADDR_24BITS;
			pstParam->ulRegAddr = (pu8AddrIIC[0]<<16) |
			(pu8AddrIIC[1]<<8) | (pu8AddrIIC[2]);
			break;
	default:
			break;
	}
}

static EN_KER_IIC_CLOCK  _kernel_iic_clockmapping_p(u32 u32Frq)
{
	EN_KER_IIC_CLOCK u32Value = EN_KER_IIC_CLOCK_20K;

	if (u32Frq <= 20)
		u32Value = EN_KER_IIC_CLOCK_20K;
	else if (u32Frq <= 30)
		u32Value = EN_KER_IIC_CLOCK_30K;
	else if (u32Frq <= 40)
		u32Value = EN_KER_IIC_CLOCK_40K;
	else if (u32Frq <= 50)
		u32Value = EN_KER_IIC_CLOCK_50K;
	else if (u32Frq <= 60)
		u32Value = EN_KER_IIC_CLOCK_60K;
	else if (u32Frq <= 70)
		u32Value = EN_KER_IIC_CLOCK_70K;
	else if (u32Frq <= 80)
		u32Value = EN_KER_IIC_CLOCK_80K;
	else if (u32Frq <= 90)
		u32Value = EN_KER_IIC_CLOCK_90K;
	else if (u32Frq <= 100)
		u32Value = EN_KER_IIC_CLOCK_100K;
	else if (u32Frq <= 200)
		u32Value = EN_KER_IIC_CLOCK_200K;
	else if (u32Frq <= 300)
		u32Value = EN_KER_IIC_CLOCK_300K;
	else if (u32Frq <= 330)
		u32Value = EN_KER_IIC_CLOCK_330K;
	else if (u32Frq <= 400)
		u32Value = EN_KER_IIC_CLOCK_400K;
	else if (u32Frq <= 500)
		u32Value = EN_KER_IIC_CLOCK_500K;
	else if (u32Frq <= 600)
		u32Value = EN_KER_IIC_CLOCK_600K;
	else if (u32Frq <= 700)
		u32Value = EN_KER_IIC_CLOCK_700K;
	else if (u32Frq <= 800)
		u32Value = EN_KER_IIC_CLOCK_800K;
	else
		u32Value = EN_KER_IIC_CLOCK_100K;

	return u32Value;
}

#endif

MODULE_DESCRIPTION("NOVATEK NT726xx I2C bus adapter");
MODULE_AUTHOR("NOVATEK");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_ALIAS("platform:" DRIVER_NAME);
