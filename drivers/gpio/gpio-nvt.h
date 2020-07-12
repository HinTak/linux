/* ********************************************************************* */

/*  File Name: ntgpio.h */

/*  Description: gpio driver */

/* ********************************************************************* */

#ifndef _NTGPIO_H
#define _NTGPIO_H


#include <linux/platform_device.h>	/* struct device* */
#include <linux/gpio.h>
#include <linux/ioctl.h>
#if defined(CONFIG_ARCH_NVT72172)
#include "gpio-nvt-chip-172.h"
#elif defined(CONFIG_ARCH_NVT72673)
#include "gpio-nvt-chip-673.h"
#elif defined(CONFIG_ARCH_NVT72671D)
#include "gpio-nvt-chip-671d.h"
#endif

/* for ErrMsg output standard	added 050915 */
#define GPIO_DBG_NONE_BG	"\x1b[0m"
#define GPIO_DBG_RED_BG	"\x1b[0;32;41m"
#define GPIO_MSG_PREFIX_ERR GPIO_DBG_RED_BG"[ERR_DRV_GPIO]"GPIO_DBG_NONE_BG

#ifdef DEBUG
#define GPIO_DBG_LOG(fmt, ...)		\
	pr_err("[%s():%d]: " fmt,		\
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define GPIO_DBG_LOG(fmt, ...)
#endif

#define GPIO_INF_LOG(fmt, ...)		\
	pr_err("[ERR_BSP_GPIO][%s():%d] " fmt,		\
		__func__, __LINE__, ##__VA_ARGS__)


#define BIT_0           (1L<<0)
#define BIT_1           (1L<<1)
#define BIT_2           (1L<<2)
#define BIT_3           (1L<<3)
#define BIT_4           (1L<<4)
#define BIT_5           (1L<<5)
#define BIT_6           (1L<<6)
#define BIT_7           (1L<<7)
#define BIT_8           (1L<<8)
#define BIT_9           (1L<<9)
#define BIT_10          (1L<<10)
#define BIT_11          (1L<<11)
#define BIT_12          (1L<<12)
#define BIT_13          (1L<<13)
#define BIT_14          (1L<<14)
#define BIT_15          (1L<<15)
#define BIT_16          (1L<<16)
#define BIT_17          (1L<<17)
#define BIT_18          (1L<<18)
#define BIT_19          (1L<<19)
#define BIT_20          (1L<<20)
#define BIT_21          (1L<<21)
#define BIT_22          (1L<<22)
#define BIT_23          (1L<<23)
#define BIT_24          (1L<<24)
#define BIT_25          (1L<<25)
#define BIT_26          (1L<<26)
#define BIT_27          (1L<<27)
#define BIT_28          (1L<<28)
#define BIT_29          (1L<<29)
#define BIT_30          (1L<<30)
#define BIT_31          (1L<<31)

#define _AGPIO_CF_BUS_BIT					0x00000000
#define _AGPIO_SDMMC_BUS_BIT				0x00000001
#define _AGPIO_SMC_BUS_BIT					0x00000002
#define _AGPIO_MSPRO_BUS_BIT				0x00000003
#define _AGPIO_SPI_BUS_BIT					0x00000004
#define _AGPIO_DEBUG_GROUP_BUS_BIT		0x00000005
#define _AGPIO_CI_BUS_BIT					0x00000006
#define _AGPIO_HW_SPI_BOOTER_BUS_BIT        0x00000007

#define KER_GPIO_HDMI_IDENT_PHY_ADDR	(0xFc040c10)
#define KER_GPIO_MHL_IDENT_PHY_ADDR	(0xFc040424)
#define GPIO_FUNCTION_MUX_MAX			(4)

struct ST_GPIO_GROUP_INFO {
	unsigned char u8group_size;
	unsigned char u8group_start;
	unsigned char u8group_end;
};

struct ST_KER_GPIO_ALT_MUX_MAP
{
	unsigned char u8GrpOffset;
	unsigned int u32AltMux;
};

typedef struct GPIO_DS_REG_s
{
    //reg0x00
    unsigned int ulGPIO_DS0;

    //reg0x04
    unsigned int ulGPIO_DS1;

} GPIO_DS_REG_t,*pGPIO_DS_REG_t;

enum EN_GPIO_MUX_SELECT_DEFIN {
	EN_GPIO_MUX_SELECT_MASK,
	EN_GPIO_MUX_SELECT_FUN1,
	EN_GPIO_MUX_SELECT_FUN2,
	EN_GPIO_MUX_SELECT_FUN3,
	EN_GPIO_MUX_SELECT_FUN4,
	EN_GPIO_MUX_SELECT_FUN5,
	EN_GPIO_MUX_SELECT_FUN6,
	EN_GPIO_MUX_SELECT_FUN7,
	EN_GPIO_MUX_SELECT_FUN8,
	EN_GPIO_MUX_SELECT_FUN9,
	EN_GPIO_MUX_SELECT_FUN10,
	EN_GPIO_MUX_SELECT_MAX
};


#define MAX_GPIO_LINES		135


struct nt726xx_gpio_reg_offs {
	unsigned short revision;
	unsigned short direction;
	unsigned short datain;
	unsigned short dataout;
	unsigned short set_dataout;
	unsigned short clr_dataout;
	unsigned short irqstatus;
	unsigned short irqstatus2;
	unsigned short irqstatus_raw0;
	unsigned short irqstatus_raw1;
	unsigned short irqenable;
	unsigned short irqenable2;
	unsigned short set_irqenable;
	unsigned short clr_irqenable;
	unsigned short debounce;
	unsigned short debounce_en;
	unsigned short ctrl;
	unsigned short wkup_en;
	unsigned short leveldetect0;
	unsigned short leveldetect1;
	unsigned short risingdetect;
	unsigned short fallingdetect;
	unsigned short irqctrl;
	unsigned short edgectrl1;
	unsigned short edgectrl2;
	unsigned short pinctrl;

	bool irqenable_inv;
};

struct nt726xx_gpio_platform_data {
	int bank_type;
	int bank_width;
	bool loses_context;	/* whether the bank would ever lose context */
	unsigned int non_wakeup_gpios;
	struct nt726xx_gpio_reg_offs *regs;

	/* Return context loss count due to PM states changing */
	int (*get_context_loss_count)(struct device *dev);
};

#define GPIO_INT_EN_REG    0xFD0D0800
#define GPIO_INT_LV_REG    0xFD0D0804
#define GPIO_INT_ST_REG    0xFD0F0808

struct GPIO_REG_s {
	/* reg0x00 */
	unsigned long ulGPIOClear;

	/* reg0x04 */
	unsigned long ulGPIOSet;

	/* reg0x08 */
	unsigned long ulGPIODir;

	/* reg0x0C */
	unsigned long ulGPIODrvPin1;

	/* reg0x10 */
	unsigned long ulGPIODrvPin2;

	/* reg0x14 */
	unsigned long reserver2;

	/* reg0x18 */
	unsigned long ulGPIOPD;

	/* reg0x1C */
	unsigned long ulGPIOPU;

	/* reg0x20 */
	unsigned long ulGPIOMUX[4];
};

#ifndef TRUE
#define TRUE        (1)                  /*!< Define TRUE 1 */
#endif

#ifndef ENABLE
#define ENABLE      (1)                  /*!< Define ENABLE 1 */
#endif

#ifndef FALSE
#define FALSE       (0)                  /*!< Define FALSE 0 */
#endif

#ifndef DISABLE
#define DISABLE     (0)                  /*!< Define DISABLE 0 */
#endif



#define KER_GPIO_IOCTLID					'g'


/*  General purpose commands of ioctl system call */
#define KER_GPIO_IOINITIALISE\
			_IOW(KER_GPIO_IOCTLID, 1, ST_KER_GPIO_INIT_PARAM)
#define KER_GPIO_IOTERMINATE\
			_IO(KER_GPIO_IOCTLID, 2)
#define KER_GPIO_IOOPEN_GPIO\
			_IOW(KER_GPIO_IOCTLID, 10, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOCLOSE_GPIO\
			_IOW(KER_GPIO_IOCTLID, 11, enum EN_KER_GPIO_PIN)
#define KER_GPIO_IOSET_LEVEL\
			_IOW(KER_GPIO_IOCTLID, 12, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOGET_LEVEL\
			_IOWR(KER_GPIO_IOCTLID, 13, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOSET_DIR\
			_IOW(KER_GPIO_IOCTLID, 14, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOGET_DIR\
			_IOWR(KER_GPIO_IOCTLID, 15, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOSET_ALTMODE\
			_IOW(KER_GPIO_IOCTLID, 16, ST_KER_GPIO_OP_PARAM)
#define KER_GPIO_IOGET_ALTMODE\
			_IOWR(KER_GPIO_IOCTLID, 17, ST_KER_GPIO_OP_PARAM)


/*  Jay Hsu @ 20110611 : Set RMII Reference Clock Outuput */
#define KER_GPIO_IOSET_RMIIREFCLK\
			_IOW(KER_GPIO_IOCTLID, 19, ST_KER_GPIO_RMIICLK_PARAM)

#define KER_GPIO_IOCONFIG_PAD\
			_IOW(KER_GPIO_IOCTLID, 20, ST_KER_GPIO_PAD_PARAM)
#define KER_GPIO_IOCONFIG_INT\
			_IOW(KER_GPIO_IOCTLID, 21, ST_KER_GPIO_INT_PARAM)
#define KER_GPIO_IOFUN_INITIALISE\
			_IOW(KER_GPIO_IOCTLID, 22, ST_KER_GPIO_OP_PARAM)

#define KER_GPIO_IO_MAPWRAPPER\
			_IOW(KER_GPIO_IOCTLID, 30, ST_KER_GPIO_MAP_PARAM)
#define KER_GPIO_IOGET_PINNAME\
		_IOWR(KER_GPIO_IOCTLID, 31, ST_KER_GPIO_FUN_PIN_NAME_PARAM)


typedef struct _ST_KER_GPIO_INIT_PARAM {
	unsigned int u32GPIOMode[EN_KER_MIPS_GPIO_GROUP_MIPS_MAX];
	unsigned int u32Direction[EN_KER_MIPS_GPIO_GROUP_MIPS_MAX];
	unsigned int u32Level[EN_KER_MIPS_GPIO_GROUP_MIPS_MAX];
	unsigned int u32ActiveMode[EN_KER_MIPS_GPIO_GROUP_MIPS_MAX];
} ST_KER_GPIO_INIT_PARAM;

typedef struct _ST_KER_GPIO_ALT_INIT {
	enum EN_KER_GPIO_PIN	enPin;
	unsigned char			u8enMode;
} ST_KER_GPIO_ALT_INIT;

enum EN_KER_GPIO_RMIICLK {
	EN_KER_GPIO_RMIICLK_ORG,
	EN_KER_GPIO_RMIICLK_I2SWS2,
};

struct ST_KER_GPIO_RMIICLK_PARAM {
	unsigned int	u32RMIIREFCLK;
};

typedef struct _ST_KER_GPIO_OP_PARAM {
	enum EN_KER_GPIO_PIN enPin;
	union {
		bool b8Value;
		unsigned char u8Value;
	};
} ST_KER_GPIO_OP_PARAM;

struct ST_KER_GPIO_MAP_PARAM {
	unsigned char u8IOCount;
	unsigned char u8IOTbl[EN_KER_GPIO_PIN_TOTAL];

};

struct ST_KER_GPIO_OP_FUN_INIT {
	unsigned int ulCount;
	struct ST_KER_GPIO_ALT_INIT *pstGPIOAltInit;
};

#define GPIO_PAD_DRIVING_MASK_BIT (BIT_0 | BIT_1)

enum EN_KER_GPIO_PAD_CFG {
	EN_KER_GPIO_PAD_CFG_DEFAULT_SETTING = 0x8000,
	EN_KER_GPIO_PAD_CFG_PULL_UP = 0x4000,
	EN_KER_GPIO_PAD_CFG_PULL_DOWN = 0x2000,
	EN_KER_GPIO_PAD_CFG_SLEW_RATE = 0x1000,
	EN_KER_GPIO_PAD_CFG_SCHMITT_TRIGGER = 0x0800,
	EN_KER_GPIO_PAD_CFG_IDDQ_TEST_MODE = 0x0400,
	EN_KER_GPIO_PAD_CFG_DRIVING = 0x0200,

	EN_KER_GPIO_PAD_CFG_EN_PULL_UP = 0x40,
	EN_KER_GPIO_PAD_CFG_DIS_PULL_UP = 0x00,

	EN_KER_GPIO_PAD_CFG_EN_PULL_DOWN = 0x20,
	EN_KER_GPIO_PAD_CFG_DIS_PULL_DOWN = 0x00,

	EN_KER_GPIO_PAD_CFG_SLOW_SLEW_RATE = 0x10,
	EN_KER_GPIO_PAD_CFG_FAST_SLEW_RATE = 0x00,

	EN_KER_GPIO_PAD_CFG_EN_SCHMITT_TRIGGER = 0x08,
	EN_KER_GPIO_PAD_CFG_DIS_SCHMITT_TRIGGER = 0x00,

	EN_KER_GPIO_PAD_CFG_EN_IDDQ_TEST_MODE = 0x04,
	EN_KER_GPIO_PAD_CFG_DIS_IDDQ_TEST_MODE = 0x00,

	EN_KER_GPIO_PAD_CFG_DRIVING_10_0mA = 0x03,
	EN_KER_GPIO_PAD_CFG_DRIVING_7_5mA = 0x02,
	EN_KER_GPIO_PAD_CFG_DRIVING_5_0mA = 0x01,
	EN_KER_GPIO_PAD_CFG_DRIVING_2_5mA = 0x00

};

typedef struct _ST_KER_GPIO_PAD_PARAM {
	enum EN_KER_GPIO_PIN enPin;
	unsigned short u16Mode;
	unsigned char u8Driving;
} ST_KER_GPIO_PAD_PARAM;

typedef enum _EN_KER_GPIO_ALT_MODE {
	EN_KER_GPIO_ALT_MODE_GPIO,
	EN_KER_GPIO_ALT_MODE_ALTERNATIVE,
	EN_KER_GPIO_ALT_MODE_FUN1 = EN_KER_GPIO_ALT_MODE_ALTERNATIVE,
	EN_KER_GPIO_ALT_MODE_FUN2,
	EN_KER_GPIO_ALT_MODE_FUN3,
	EN_KER_GPIO_ALT_MODE_FUN4,
	EN_KER_GPIO_ALT_MODE_FUN5,
	EN_KER_GPIO_ALT_MODE_FUN6,
	EN_KER_GPIO_ALT_MODE_FUN7,

	EN_KER_GPIO_ALT_MODE_TOTAL,
} EN_KER_GPIO_ALT_MODE;

typedef enum _EN_KER_GPIO_INT_TYPE {
	EN_KER_GPIO_NO_INT = 0,
	EN_KER_GPIO_FALLING_EDGE_INT,
	EN_KER_GPIO_RISING_EDGE_INT,
	EN_KER_GPIO_LOW_LEVEL_INT,
	EN_KER_GPIO_HIGH_LEVEL_INT
} EN_KER_GPIO_INT_TYPE;

typedef enum _EN_KER_GPIO_DIRECTION {
	EN_KER_GPIO_DIRECTION_INPUT,
	EN_KER_GPIO_DIRECTION_OUTPUT,

	EN_KER_GPIO_DIRECTION_TOTAL
} EN_KER_GPIO_DIRECTION;

typedef enum _EN_KER_GPIO_PAD_673_CFG
{
	EN_KER_GPIO_PAD_673_CFG_DRIVING_3_7mA          		= 0x03,	
	EN_KER_GPIO_PAD_673_CFG_DRIVING_3_1mA           	= 0x02,
	EN_KER_GPIO_PAD_673_CFG_DRIVING_2_5mA           	= 0x01,	
	EN_KER_GPIO_PAD_673_CFG_DRIVING_1_8mA           	= 0x00,
} EN_KER_GPIO_PAD_673_CFG;

typedef enum _EN_KER_GPIO_PAD_671_CFG
{
	EN_KER_GPIO_PAD_671_CFG_DRIVING_4_0mA          		= 0x03,	
	EN_KER_GPIO_PAD_671_CFG_DRIVING_3_0mA           	= 0x02,
	EN_KER_GPIO_PAD_671_CFG_DRIVING_2_5mA           	= 0x01,	
	EN_KER_GPIO_PAD_671_CFG_DRIVING_2_0mA           	= 0x00,
} EN_KER_GPIO_PAD_671_CFG;

typedef enum _EN_KER_GPIO_LEVEL {
	EN_KER_GPIO_LEVEL_HIGH,
	EN_KER_GPIO_LEVEL_LOW,
	EN_KER_GPIO_LEVEL_NONE,

	EN_KER_GPIO_LEVEL_TOTAL
} EN_KER_GPIO_LEVEL;

struct ST_KER_GPIO_INT_PARAM {
	enum EN_KER_GPIO_PIN	enPin;
	EN_KER_GPIO_INT_TYPE enIntType;
};

struct ST_KER_GPIO_FUN_PIN_NAME_PARAM {
	enum EN_KER_GPIO_PIN	enPin;
	EN_KER_GPIO_ALT_MODE enGpioFun_mode;
	char enGpio_mode[24];
	char enNormal[24];
	char enActive[24];
	char enPad_info[24];
	char enInt_type[24];
	const char *enName;
	bool       b8level;
	bool       b8Direction;
	unsigned char u8FunMode;
};


/* ********************************/

/*  Function Name: ntgpio_ActMemBusSel */

/*  Description:   select active memory bus */

/*  Parameters:    busmode - */

/*  Returns:       TRUE if successful */
/*                 FALSE if fail */

/* ****************************/
extern int  ntgpio_ActMemBusSel(unsigned char busmode);

/* ****************************/

/*  Function Name: ntgpio_OpenGPIO */

/*  Description:   open gpio pin */

/*  Parameters:    pin - pin id */
/*                 gpiomode - TRUE : gpio mode */
/*                            FALSE : alternative mode */

/*  Returns:       TRUE if successful */
/*                 FALSE if fail */

/* ****************************/
/* extern int	ntgpio_OpenGPIO( enum EN_KER_GPIO_PIN pin, bool gpiomode ); */

/* ****************************/

/*  Function Name: ntgpio_CloseGPIO */

/*  Description:   close gpio pin */

/*  Parameters:    pin - pin id */

/*  Returns:       none */

/* ****************************/


#define GPIOOpen(x) /*  null */
#define GPIOClose(x) /*  null */

#define GPIOActMemBusSel(x)  ntgpio_ActMemBusSel(x)

#define AGPIO0SetA0Selt(x) /*  null */

int  KER_GPIO_DrvInit(void);
void KER_GPIO_DrvExit(void);

int __init nt726xx_gpio_init(void);
void __exit nt726xx_gpio_exit(void);

int nt726xx_gpio_request(struct gpio_chip *chip, unsigned offset);
void nt726xx_gpio_free(struct gpio_chip *chip, unsigned offset);
int nt726xx_gpio_direction_in(struct gpio_chip *chip, unsigned offset);
int nt726xx_gpio_direction_out(struct gpio_chip *chip,
	unsigned offset, int value);
int nt726xx_gpio_get(struct gpio_chip *chip, unsigned offset);
void nt726xx_gpio_set(struct gpio_chip *chip, unsigned offset, int value);

int nt726xx_pinctrl_get(struct gpio_chip *chip, unsigned offset);
int nt726xx_pinctrl_set(struct gpio_chip *chip, unsigned offset, int value);

int nt726xx_ConfigPadMode_set(struct gpio_chip *chip,
	unsigned offset, int value);

void gpio_IOMuxSel(enum EN_KER_GPIO_PIN pin, unsigned int *pData);
static bool gpio_default_function_pin_search(enum EN_KER_GPIO_PIN enPin);

void Control_amp_nreset_inorout(unsigned char inorout);
void Control_amp_nreset_Low(void);
void Control_amp_nreset_High(void);

#endif /* _UIO_NTGPIO_H END */
