#ifndef __I2C_NT726XX_H
#define __I2C_NT726XX_H



#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/ioctl.h>
#include <linux/ker_iic_syscalls.h>
#include <linux/gpio.h>/*Tuner Nreset use*/


#ifdef DEBUG
#define I2C_DBG_LOG(fmt, ...)		\
	pr_err("[%s():%d]: " fmt,		\
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define I2C_DBG_LOG(fmt, ...)
#endif

#define I2C_INF_LOG(fmt, ...)		\
	pr_err("[ERR_BSP_I2C][%s():%d] " fmt,		\
		__func__, __LINE__, ##__VA_ARGS__)

#ifndef TRUE
#define TRUE		 (1)	/*!< Define TRUE 1 */
#endif

#ifndef ENABLE
#define ENABLE		(1)		/*!< Define ENABLE 1 */
#endif

#ifndef FALSE
#define FALSE		 (0)	/*!< Define FALSE 0 */
#endif

#ifndef DISABLE
#define DISABLE	 (0)		/*!< Define DISABLE 0 */
#endif


#define NTIIC_NAME			"ntkiic0"
#define NTIIC_NAME1			"ntkiic1"
#define NTIIC_NAME2			"ntkiic2"
#define NTIIC_NAME3			"ntkiic3"
#define NTIIC_NAME4			"ntkiic4"
#define NTIIC_NAME5			"ntkiic5"

#if 0

#undef DEBUG /* undef me for production */


#endif
#define I2C_SUSPEND_ENABLE 0

#define I2C_IRQ 99
/*
#define BIT_0           0x00000001
#define BIT_1           0x00000002
#define BIT_2           0x00000004
#define BIT_3           0x00000008
#define BIT_4           0x00000010
#define BIT_5           0x00000020
#define BIT_6           0x00000040
#define BIT_7           0x00000080
#define BIT_8           0x00000100
#define BIT_9           0x00000200
#define BIT_10          0x00000400
#define BIT_11          0x00000800
#define BIT_12          0x00001000
#define BIT_13          0x00002000
#define BIT_14          0x00004000
#define BIT_15          0x00008000
#define BIT_16          0x00010000
#define BIT_17          0x00020000
#define BIT_18          0x00040000
#define BIT_19          0x00080000
#define BIT_20          0x00100000
#define BIT_21          0x00200000
#define BIT_22          0x00400000
#define BIT_23          0x00800000
#define BIT_24          0x01000000
#define BIT_25          0x02000000
#define BIT_26          0x04000000
#define BIT_27          0x08000000
#define BIT_28          0x10000000
#define BIT_29          0x20000000
#define BIT_30          0x40000000
#define BIT_31          0x80000000
*/

/* bit mapping of I2C control register */
#define _I2C_CTRL_OPERATING                 0x00000001
#define _I2C_CTRL_TRIGGER                   0x00000001
#define _I2C_CTRL_BUSY                      0x00000002
#define _I2C_CTRL_I2C_ENABLE                0x00000004
#define _I2C_CTRL_I2C_DISABLE               0x00000000
#define _I2C_CTRL_MODE_SEL_MASK             0x00000030
#define _I2C_CTRL_SUBADDR_ENABLE            0x00000040
#define _I2C_CTRL_SUBADDR_DISABLE           0x00000000
#define _I2C_CTRL_REPEAT_ENABLE             0x00000080
#define _I2C_CTRL_REPEAT_DISABLE            0x00000000
#define _I2C_CTRL_READ_OPERATION				  BIT_8
#define _I2C_CTRL_SLAVE_ADDRESS_ONLY_MASK   0x0000FE00
#define _I2C_CTRL_SLAVE_ADDRESS_MASK        0x0000FF00
#define _I2C_CTRL_16BITSUBADDR_ENABLE		  BIT_16
#define _I2C_CTRL_16BITSUBADDR_DISABLE		  0
#define _I2C_CTRL_24BITSUBADDR_ENABLE		  BIT_17
#define _I2C_CTRL_24BITSUBADDR_DISABLE		0
#define _I2C_CTRL_32BITSUBADDR_ENABLE		  BIT_18
#define _I2C_CTRL_32BTISUBADDR_DISABLE		  0
#define _I2C_CTRL_SUBADDR_LITTLE_ENDIAN_EN  BIT_19
#define _I2C_CTRL_SUBADDR_BIG_ENDIAN_ENABLE 0

#define _I2C_CTRL_CLOCK_DUTY_ENABLE			BIT_21
#define _I2C_CTRL_HIGH_DUTY_MASK			0x0000FFF0


#define _I2C_CTRL_ACK_STATUS_MASK           0x01000000
#define _I2C_CTRL_CLOCK_STRETCH_ENABLE		  BIT_27
#define _I2C_CTRL_CLOCK_STRETCH_DISABLE	  0
#define _I2C_CTRL_MASTER_CLK_STRETCH_ENABLE BIT_28
#define _I2C_CTRL_MASTER_CLK_STRETCH_DISABLE 0

#define _I2C_CTRL_DATA_SIZE_MASK			0xFFFFFF00

#define _I2C_NORMAL_MODE                    0x00000000
#define _I2C_BURST_MODE                     0x00000010
#define _I2C_MANUAL_MODE                    0x00000020

#define KER_IIC_FLAG_SUBADDR_MASK             (0xE0000000)
#define KER_IIC_FLAG_24BITS                   (0x80000000)
#define KER_IIC_FLAG_16BITS                   (0x40000000)
#define KER_IIC_FLAG_NO_ADDR                  (0x20000000)



#define	_REG_CONTROLREG			0xC0		/* 0xC0 */
#define	_REG_CLOCK				0xC4		/* 0xC4 */
#define	_REG_ACKCTL				0xC8		/* 0xC8 */
#define	_REG_SIZE				0xCC		/* 0xCC */
#define	_REG_DATAFIFO1_1		0xD0		/* 0xD0 ~ 0xDF */
#define	_REG_DATAFIFO1_2		0xD4		/* 0xD0 ~ 0xDF */
#define	_REG_DATAFIFO1_3		0xD8		/* 0xD0 ~ 0xDF */
#define	_REG_DATAFIFO1_4		0xDC		/* 0xD0 ~ 0xDF */
#define	_REG_SBADDR				0xE0		/* 0xE0 */
#define	_REG_PINGPONGCTRL		0xE4		/* 0xE4 */
#define	_REG_INTCTRL			0xE8		/* 0xE8 */
#define	_REG_DataFIFO2_1		0xEC		/* 0xEC ~ 0xF8 */
#define	_REG_DataFIFO2_2		0xF0		/* 0xEC ~ 0xF8 */
#define	_REG_DataFIFO2_3		0xF4		/* 0xEC ~ 0xF8 */
#define	_REG_DATAFIFO2_4		0xF8		/* 0xEC ~ 0xF8 */
#define	_REG_CLKDUTY			0xFC		/* 0xFC */

#define NT726xx_FIFO_1  1
#define NT726xx_FIFO_2  2

#define _BADDR_MASTER0			(0xFD000000+0x000)
#define _BADDR_MASTER1			(_BADDR_MASTER0+0x100)
#define _BADDR_MASTER2			(_BADDR_MASTER0+0x200)
#define _BADDR_MASTER3			(_BADDR_MASTER0+0x300)
#define _BADDR_MASTER4			(_BADDR_MASTER0+0x400)
#define _BADDR_MASTER5			(_BADDR_MASTER0+0x500)

/*Tuner Nreset use*/
extern int nt726xx_gpio_request(struct gpio_chip *chip, unsigned offset);
extern void nt726xx_gpio_set(struct gpio_chip *chip, unsigned offset,
	int value);
extern void nt726xx_gpio_free(struct gpio_chip *chip, unsigned offset);

enum KER_IIC_REC_DEF_en {
	EN_PMI_IIC_INIT = 0,
	EN_PMI_IIC_CMD,

	EN_PMI_IIC_INVALID

};

struct _ker_iic_cmd_rec_t {
	enum EN_KER_IIC_BUS				enBus;
	enum EN_KER_IIC_CLOCK			enClk;
	enum EN_KER_IIC_SUBADDR_MODE	enSubAddrMode;
	unsigned char					u8SlaveAddr;
	unsigned long					u32SubAddr;
	unsigned long					u32NumOfBytes;
	unsigned char					*pu8Buf;
};


struct ST_HWI2C {
	wait_queue_head_t       wait;
	unsigned int            irq_num;
	unsigned int            irq_init;
	unsigned char           *name;
};


struct ST_KEEP_MSGINFO {
	enum EN_KER_IIC_SUBADDR_MODE stSubAdrMode;
	u32 stSubAdr;
};

struct ST_I2C_INT_MAILBOX {
	unsigned int            msg_num;
	unsigned int            irq_flag;
	unsigned int                u32Buffer[4];
	unsigned char               *wPtr;
	int                         NumOfBytes;
	int                         PingPongIndex;
};

int __init i2c_nt726xx_platform_init(void);
void __exit i2c_nt726xx_platform_exit(void);

#endif	/* __I2C_726XX_H */
