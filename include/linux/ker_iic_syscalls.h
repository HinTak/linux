/********************************************************************/

 /*File Name: ker_iic_syscalls.h*/

 /*Description: iic*/

/********************************************************************/

#ifndef _KER_IIC_SYSCALLS_H
#define _KER_IIC_SYSCALLS_H

/********************************************************************/
 /*If you are adding new ioctl's to the kernel, you should use the _IO*/

    /*_IO    an ioctl with no parameters*/
    /*_IOW   an ioctl with write parameters (copy_from_user)*/
    /*_IOR   an ioctl with read parameters  (copy_to_user)*/
    /*_IOWR  an ioctl with both write and read parameters.*/
/********************************************************************/
/*#define I2C_SUSPEND_ENABLE*/
#define KER_IIC_IOCTLID						'i'


/*General purpose commands of ioctl system call*/
#define KER_IIC_IOOPEN\
			_IOW(KER_IIC_IOCTLID, 1, ST_KER_IIC_OPEN_PARAM)
#define KER_IIC_IOCLOSE\
			_IOW(KER_IIC_IOCTLID, 2, EN_KER_IIC_BUS)

#define KER_IIC_IOREAD\
			_IOW(KER_IIC_IOCTLID, 3, ST_KER_IIC_OP_PARAM)
#define KER_IIC_IOWRITE\
			_IOW(KER_IIC_IOCTLID, 4, ST_KER_IIC_OP_PARAM)

#define KER_IIC_IOREAD_OVER_4K\
			_IOW(KER_IIC_IOCTLID, 5, ST_KER_IIC_OP_PARAM)
#define KER_IIC_IOWRITE_OVER_4K\
			_IOW(KER_IIC_IOCTLID, 6, ST_KER_IIC_OP_PARAM)
#define KER_IIC_IOREAD_STAUUS\
			_IOWR(KER_IIC_IOCTLID, 7, ST_KER_IIC_OP_PARAM)
#define KER_IIC_OVER_4K_SIZE					4096

#define KER_SIIC_IOOPEN\
			_IOW(KER_IIC_IOCTLID, 101, ST_KER_IIC_OPEN_PARAM)
#define KER_SIIC_IOCLOSE\
			_IOW(KER_IIC_IOCTLID, 102, EN_KER_IIC_BUS)

#define KER_SIIC_IOREAD\
			_IOW(KER_IIC_IOCTLID, 103, ST_KER_IIC_OP_PARAM)
#define KER_SIIC_IOWRITE\
			_IOW(KER_IIC_IOCTLID, 104, ST_KER_IIC_OP_PARAM)


#define KER_IIC_IO_MAGIC					900
#define KER_IIC_IO_SET_CLK\
			_IOW(KER_IIC_IO_MAGIC, 1, unsigned int)
#define KER_IIC_IO_SET_CHANNEL\
			_IOW(KER_IIC_IO_MAGIC, 2, unsigned int)
#define KER_IIC_IO_SET_BITNUM\
			_IOW(KER_IIC_IO_MAGIC, 3, unsigned int)
#define KER_IIC_IO_SET_SUBADDR\
			_IOW(KER_IIC_IO_MAGIC, 4, unsigned long)

#ifndef BIT_0
#define BIT_0           0x00000001
#endif
#ifndef BIT_1
#define BIT_1           0x00000002
#endif
#ifndef BIT_2
#define BIT_2           0x00000004
#endif
#ifndef BIT_3
#define BIT_3           0x00000008
#endif
#ifndef BIT_4
#define BIT_4           0x00000010
#endif
#ifndef BIT_5
#define BIT_5           0x00000020
#endif
#ifndef BIT_6
#define BIT_6           0x00000040
#endif
#ifndef BIT_7
#define BIT_7           0x00000080
#endif
#ifndef BIT_8
#define BIT_8           0x00000100
#endif
#ifndef BIT_9
#define BIT_9           0x00000200
#endif
#ifndef BIT_10
#define BIT_10          0x00000400
#endif
#ifndef BIT_11
#define BIT_11          0x00000800
#endif
#ifndef BIT_12
#define BIT_12          0x00001000
#endif
#ifndef BIT_13
#define BIT_13          0x00002000
#endif
#ifndef BIT_14
#define BIT_14          0x00004000
#endif
#ifndef BIT_15
#define BIT_15          0x00008000
#endif
#ifndef BIT_16
#define BIT_16          0x00010000
#endif
#ifndef BIT_17
#define BIT_17          0x00020000
#endif
#ifndef BIT_18
#define BIT_18          0x00040000
#endif
#ifndef BIT_19
#define BIT_19          0x00080000
#endif
#ifndef BIT_20
#define BIT_20          0x00100000
#endif
#ifndef BIT_21
#define BIT_21          0x00200000
#endif
#ifndef BIT_22
#define BIT_22          0x00400000
#endif
#ifndef BIT_23
#define BIT_23          0x00800000
#endif
#ifndef BIT_24
#define BIT_24          0x01000000
#endif
#ifndef BIT_25
#define BIT_25          0x02000000
#endif
#ifndef BIT_26
#define BIT_26          0x04000000
#endif
#ifndef BIT_27
#define BIT_27          0x08000000
#endif
#ifndef BIT_28
#define BIT_28          0x10000000
#endif
#ifndef BIT_29
#define BIT_29          0x20000000
#endif
#ifndef BIT_30
#define BIT_30          0x40000000
#endif
#ifndef BIT_31
#define BIT_31          0x80000000
#endif

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

enum EN_KER_IIC_SUBADDR_MODE {
	EN_KER_IIC_NO_SUBADDR = 0,
	EN_KER_IIC_SUBADDR_8BITS,
	EN_KER_IIC_SUBADDR_16BITS,
	EN_KER_IIC_SUBADDR_24BITS,
	EN_KER_IIC_SUBADDR_32BITS,

	EN_KER_IIC_SUBADDR_TOTAL
};
#define KER_IIC_FLAG_CLOCK_MASK               (0x0F000000)
#define KER_IIC_FLAG_CLOCK_100K				  (0x00000000)
#define KER_IIC_FLAG_CLOCK_200K				  (0x01000000)
#define KER_IIC_FLAG_CLOCK_300K				  (0x02000000)
#define KER_IIC_FLAG_CLOCK_330K				  (0x10000000)
#define KER_IIC_FLAG_CLOCK_400K				  (0x03000000)
#define KER_IIC_FLAG_CLOCK_500K				  (0x04000000)
#define KER_IIC_FLAG_CLOCK_600K				  (0x05000000)
#define KER_IIC_FLAG_CLOCK_700K				  (0x06000000)
#define KER_IIC_FLAG_CLOCK_800K				  (0x07000000)
#define KER_IIC_FLAG_CLOCK_90K				  (0x08000000)
#define KER_IIC_FLAG_CLOCK_80K				  (0x09000000)
#define KER_IIC_FLAG_CLOCK_70K				  (0x0A000000)
#define KER_IIC_FLAG_CLOCK_60K				  (0x0B000000)
#define KER_IIC_FLAG_CLOCK_50K				  (0x0C000000)
#define KER_IIC_FLAG_CLOCK_40K				  (0x0D000000)
#define KER_IIC_FLAG_CLOCK_30K				  (0x0E000000)
#define KER_IIC_FLAG_CLOCK_20K				  (0x0F000000)

enum EN_KER_IIC_CLOCK {
	EN_KER_IIC_CLOCK_100K = 0,
	EN_KER_IIC_CLOCK_200K,
	EN_KER_IIC_CLOCK_300K,
	EN_KER_IIC_CLOCK_330K,
	EN_KER_IIC_CLOCK_400K,
	EN_KER_IIC_CLOCK_500K,
	EN_KER_IIC_CLOCK_600K,
	EN_KER_IIC_CLOCK_700K,
	EN_KER_IIC_CLOCK_800K,
	EN_KER_IIC_CLOCK_90K,
	EN_KER_IIC_CLOCK_80K,
	EN_KER_IIC_CLOCK_70K,
	EN_KER_IIC_CLOCK_60K,
	EN_KER_IIC_CLOCK_50K,
	EN_KER_IIC_CLOCK_40K,
	EN_KER_IIC_CLOCK_30K,
	EN_KER_IIC_CLOCK_20K,
	EN_KER_IIC_CLOCK_3K,

	EN_KER_IIC_CLOCK_TOTAL
};

enum EN_KER_IIC_BUS {
	EN_KER_IIC_BUS_0 = 0,
	EN_KER_IIC_BUS_1,
	EN_KER_IIC_BUS_2,
	EN_KER_IIC_BUS_3,
	EN_KER_IIC_BUS_4,
	EN_KER_IIC_BUS_5,
	EN_KER_IIC_BUS_6,
	EN_KER_IIC_BUS_7,
	EN_KER_IIC_DEV_MAX,
	EN_KER_SW_IIC_BUS_START = EN_KER_IIC_DEV_MAX,
	EN_KER_IIC_BUS_8 = EN_KER_IIC_DEV_MAX,/*IIC_M_HDMI1_SW//IIC_M_HDMI4_SW*/
	EN_KER_IIC_BUS_9,/*IIC_M_VGA_SW //IIC_M_HDMI3_SW*/
	EN_KER_IIC_BUS_10,
	EN_KER_IIC_BUS_11,
	EN_KER_IIC_BUS_12,

	EN_KER_IIC_BUS_TOTAL
};

enum EN_KER_IIC_MODE {
	EN_KER_IIC_MODE_HW = 0,
	EN_KER_IIC_MODE_SW,

	EN_KER_IIC_MODE_TOTAL
};

enum EN_KER_IIC_CHANNEL {
	EN_KER_IIC_CHANNEL_0 = 0,
	EN_KER_IIC_CHANNEL_1,
	EN_KER_IIC_CHANNEL_2,
	EN_KER_IIC_CHANNEL_3,
	EN_KER_IIC_CHANNEL_4,
	EN_KER_IIC_CHANNEL_5,
	EN_KER_IIC_CHANNEL_6,
	EN_KER_IIC_CHANNEL_7,
	EN_KER_IIC_CHANNEL_8,
	EN_KER_IIC_CHANNEL_9,
	EN_KER_IIC_CHANNEL_10,
	EN_KER_IIC_CHANNEL_11,
	EN_KER_IIC_CHANNEL_12,

	EN_KER_IIC_CHANNEL_TOTAL
};

enum EN_KER_IIC_DUTY_LEVEL {
	EN_KER_IIC_DUTY_LEVEL_0 = 0,/*Keep original, disable*/
	EN_KER_IIC_DUTY_LEVEL_1 = 1,/*1/4*/
	EN_KER_IIC_DUTY_LEVEL_2 = 2,/*1/2*/
	EN_KER_IIC_DUTY_LEVEL_3 = 3,/*3/4*/

};

enum EN_KER_IIC_ACK_CTRL {
	EN_KER_IIC_ACK_CTRL_DISABLE = 0,
	EN_KER_IIC_ACK_CTRL_ENABLE = 1,

	EN_KER_IIC_ACK_CTRL_TOTAL
};

struct ST_KER_IIC_PIN_DESCRIPTION {
	bool			b8MIPSGPIO;
	u32				u32Pin;
};

struct ST_KER_IIC_OPEN_PARAM {
	enum EN_KER_IIC_BUS enBusID;
	enum EN_KER_IIC_MODE enBusMode;
	enum EN_KER_IIC_CHANNEL enBusChannel;
	enum EN_KER_IIC_CLOCK enBusClock;
	struct ST_KER_IIC_PIN_DESCRIPTION	stPinDATA;
	struct ST_KER_IIC_PIN_DESCRIPTION	stPinCLOCK;
	enum EN_KER_IIC_DUTY_LEVEL enBusDuty;
	enum EN_KER_IIC_ACK_CTRL   enAckCtrl;
	u8 u8AckCtrlCounter;

};

struct ST_KER_IIC_OP_PARAM {
	u8		u8SlaveAddr;
	enum EN_KER_IIC_SUBADDR_MODE enSubAddrMode;
	u8		u8NumOfControlBytes;
	u32	u32SubAddress;

	u32	u32NumOfDataBytes;
	u8		*pau8DataBytesBuf;

	s8		s8RetNumOfDataBytes;
	u8		u8Bus_ID;/*Terry Yuan add for reduce SW IIC node*/
	enum EN_KER_IIC_CLOCK enBusClock;/*add the speed of I2C*/
	const char *enName;/*MZ Jiang add for get DTS information*/
	bool enEnableBus;/*20151225 MZ add for get DTS information*/
	u8	u8GetBusID;/*20151225 MZ add for get DTS information*/
};

#endif  /* _KER_IIC_SYSCALLS_H */

