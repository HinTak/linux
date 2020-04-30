#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/cpe.h>
#include <mach/clk.h>
#include <linux/io.h>
#include <linux/semaphore.h>


static int ioremap_write(u32 address, u32 value)
{
	u32 *remap_address;

	remap_address = ioremap(address, 0x4);
	if (remap_address == NULL)
		return -1;

	writel(value, remap_address);
	iounmap(remap_address);

	return 0;
}
static int ioremap_read(u32 address)
{
	u32 *remap_address = NULL;
	u32 value = 0;

	remap_address = ioremap(address, 0x4);

	if (remap_address == NULL)
		return -1;

	value = readl(remap_address);
	iounmap(remap_address);
	return value;
}

#define u32_IPC_BUF_Addr	0xFD058020
#define CPE_TIME_OUT		5000
#define IC_REVISION 0xFD020100
#define CPE_LOG printk
#define C_OK        (0)
#define C_NOT_OK    (1)

#define READ_OTP    0x36

static u8  g_u8ipc_cmd_init = C_NOT_OK;
static struct semaphore ipc_sem;
static u32 sem_init = C_NOT_OK;
static u32 ipc_init = C_NOT_OK;

static int __init init_otp(void)
{
	if (ipc_init == C_NOT_OK) {
		CPE_LOG("[KER_IPC] OTP init\n");
		ipc_init = C_OK;
	}
	return 0;
}
module_init(init_otp);

int drv_ipc_command(u32 cmdid, u32 cmdoffset, u32 cmdlength, u32 cmdto)
{
	u32 m_u32cnt = 0;
	u32 m_cmdid = cmdid&0x3F;
	u32 m_cmdoffset = (cmdoffset&0x7F)<<17 |
				((cmdoffset&0x180)>>7)<<6 |
				((cmdoffset&0x600)>>9)<<8 |
				((cmdoffset&0x800)>>11)<<16;
	u32 m_cmdto = (cmdto&0x3F)<<10;
	u32 m_cmdlength = (cmdlength&0xFF)<<24;
	u32 m_command = m_cmdid|m_cmdoffset|m_cmdto|m_cmdlength;
	u32 m_u32tmp[4] = {0};
	int timeout;
	static u32 g_u32DRV_IPC_CMD_OFFSET = 0xFD058018;

	if (sem_init == C_NOT_OK) {
		sema_init(&ipc_sem, 1);
		sem_init = C_OK;
	}

	down(&ipc_sem);

	timeout = 0;
	ioremap_write(g_u32DRV_IPC_CMD_OFFSET, m_command);

	while ((((ioremap_read(g_u32DRV_IPC_CMD_OFFSET))&0xFF) != 0)) {
		if (m_u32cnt < CPE_TIME_OUT) {
			m_u32cnt++;
			udelay(1000);
		} else {
			CPE_LOG("[CPE_ERR] m_u8cnt = %d\n", m_u32cnt);
			timeout = 1;
			break;
		}

	}

	if (cmdid == READ_OTP) {
		for (m_u32cnt = 0; m_u32cnt < 4; m_u32cnt++) {
			m_u32tmp[m_u32cnt] =
			ioremap_read(u32_IPC_BUF_Addr + (m_u32cnt*4));
		}
		memcpy((void *)cmdto, m_u32tmp, cmdlength);
	}

	up(&ipc_sem);

	if (timeout == 0)
		return C_OK;

	return C_NOT_OK;
}
EXPORT_SYMBOL(drv_ipc_command);

static int cpe_read_otp(u32 offset, u32 len, u32 *m_p32data)
{
	return drv_ipc_command(READ_OTP,
				offset, len, (u32)m_p32data);
}
static union _st_prod_opt g_prod;
static union _st_trim g_strim;
#define OTP_PROD_OPT 1988

static void prod_otp_init(union _st_prod_opt *p_prod_opt)
{
	static u8 m_u8prod_init = C_NOT_OK;
	u32 u32tmp[2] = {0};

	if (m_u8prod_init == C_NOT_OK) {
		m_u8prod_init = C_OK;
		cpe_read_otp(OTP_PROD_OPT, 8, u32tmp);
		p_prod_opt->ST_U8[0] = (u8)(u32tmp[0]&0xFF);
		p_prod_opt->ST_U8[1] = (u8)((u32tmp[0]&0xFF00)>>8);
		p_prod_opt->ST_U8[2] = (u8)((u32tmp[0]&0xFF0000)>>16);
		p_prod_opt->ST_U8[3] = (u8)((u32tmp[0]&0xFF000000)>>24);
		if ((p_prod_opt->ST_U8bit.TRAN_SEL) == 0)
			p_prod_opt->ST_U8[4] = (u8)(u32tmp[1]&0xFF);
		else
			p_prod_opt->ST_U8[4] = (u8)((u32tmp[1]&0xFF0000)>>16);
	}
}

int ntcpe_read_valid_bit(enum en_prod_opt en_valid_type)
{
	int m_32tmp = 0;

	prod_otp_init(&g_prod);
	switch (en_valid_type) {
		case EN_VDAC_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.VDAC_TRIM1_VALID;
			break;
		}
		case EN_HDMI_RX_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.HDMI_RX_TRIM1_VALID;
			break;
		}
		case EN_LVDS_TX_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.LVDS_TX_TRIM1_VALID;
			break;
		}
		case EN_USB_01_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_01_TRIM1_VALID;
			break;
		}
		case EN_ETHERNET_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_CURRENT_TRIM1_VALID;
			break;
		}
		case EN_VDAC_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.VDAC_TRIM2_VALID;
			break;
		}
		case EN_HDMI_RX_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.HDMI_RX_TRIM2_VALID;
			break;
		}
		case EN_LVDS_TX_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.LVDS_TX_TRIM2_VALID;
			break;
		}
		case EN_USB_01_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_01_TRIM2_VALID;
			break;
		}
		case EN_ETHERNET_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_CURRENT_TRIM2_VALID;
			break;
		}
		case EN_VDAC_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.VDAC_TRIM3_VALID;
			break;
		}
		case EN_HDMI_RX_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.HDMI_RX_TRIM3_VALID;
			break;
		}
		case EN_LVDS_TX_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.LVDS_TX_TRIM3_VALID;
			break;
		}
		case EN_USB_01_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_01_TRIM3_VALID;
			break;
		}
		case EN_ETHERNET_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_CURRENT_TRIM3_VALID;
			break;
		}
		case EN_TRAN_SEL: {
			m_32tmp = g_prod.ST_U8bit.TRAN_SEL;
			break;
		}
		case EN_MID1_VALID: {
			m_32tmp = g_prod.ST_U8bit.MID1_VALID;
			break;
		}
		case EN_MID2_VALID: {
			m_32tmp = g_prod.ST_U8bit.MID2_VALID;
			break;
		}
		case EN_MID3_VALID: {
			m_32tmp = g_prod.ST_U8bit.MID3_VALID;
			break;
		}
		case EN_MID4_VALID: {
			m_32tmp = g_prod.ST_U8bit.MID4_VALID;
			break;
		}
		case EN_USB23_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_PORT23_TRIM1_VALID;
			break;
		}
		case EN_USB23_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_PORT23_TRIM2_VALID;
			break;
		}
		case EN_USB23_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.USB_PORT23_TRIM3_VALID;
			break;
		}
		case EN_ETH_RESISTANCE_TRIM1_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_RESISTANCE_TRIM1_VALID;
			break;
		}
		case EN_ETH_RESISTANCE_TRIM2_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_RESISTANCE_TRIM2_VALID;
			break;
		}
		case EN_ETH_RESISTANCE_TRIM3_VALID: {
			m_32tmp = g_prod.ST_U8bit.ETH_RESISTANCE_TRIM3_VALID;
			break;
		}
		case EN_USB_TRIM_EN: {
			m_32tmp = g_prod.ST_U8bit.USB_TRIM_EN;
			break;
		}
		default: {
			break;
		}
	}
	return m_32tmp;
}
EXPORT_SYMBOL(ntcpe_read_valid_bit);
static int cpe_load_trimming(void)
{
	cpe_read_otp(1872, 16, &(g_strim.u32Trim[0]));/*0,1,2,3*/
	cpe_read_otp(1888, 8, &(g_strim.u32Trim[4]));/*4,5*/
	return 0;
}
int ntcpe_read_trimdata(enum en_cpe_trimdata_type entype)
{
#define TRIM_DATA_ERROR  (-1)
	static u8 m_u8trim_init = C_NOT_OK;
	int m_u32Ret = 0;

	prod_otp_init(&g_prod);
	if (m_u8trim_init == C_NOT_OK) {
		m_u8trim_init = C_OK;
		cpe_load_trimming();
	}

	/*Separate*/
	switch (entype) {
		case EN_CPE_TRIMDATA_TYPE_VDAC: {
			if (g_prod.ST_U8bit.VDAC_TRIM3_VALID) {
				CPE_LOG("VDAC_TRIM3_VALID");
				m_u32Ret = g_strim.DATA.TRIM3_VDAC;
			} else if (g_prod.ST_U8bit.VDAC_TRIM2_VALID) {
				CPE_LOG("VDAC_TRIM2_VALID");
				m_u32Ret = g_strim.DATA.TRIM2_VDAC;
			} else if (g_prod.ST_U8bit.VDAC_TRIM1_VALID) {
				CPE_LOG("VDAC_TRIM1_VALID");
				m_u32Ret = g_strim.DATA.TRIM1_VDAC;
			} else {
				CPE_LOG("VDAC_TRIM Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_HDMIMHL: {
			if (g_prod.ST_U8bit.HDMI_RX_TRIM3_VALID) {
				CPE_LOG("HDMI_RX_TRIM3_VALID");
				m_u32Ret = (g_strim.DATA.TRIM3_HDMI_RX[1]<<8)|
						(g_strim.DATA.TRIM3_HDMI_RX[0]);
			} else if (g_prod.ST_U8bit.HDMI_RX_TRIM2_VALID) {
				CPE_LOG("HDMI_RX_TRIM2_VALID");
				m_u32Ret = (g_strim.DATA.TRIM2_HDMI_RX[1]<<8)|
						(g_strim.DATA.TRIM2_HDMI_RX[0]);
			} else if (g_prod.ST_U8bit.HDMI_RX_TRIM1_VALID) {
				CPE_LOG("HDMI_RX_TRIM1_VALID");
				m_u32Ret = (g_strim.DATA.TRIM1_HDMI_RX[1]<<8)|
						(g_strim.DATA.TRIM1_HDMI_RX[0]);
			} else {
				CPE_LOG("HDMI_RX_TRIM Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_LVDS: {
			if (g_prod.ST_U8bit.LVDS_TX_TRIM3_VALID) {
				CPE_LOG("LVDS_TX_TRIM3_VALID");
				m_u32Ret = g_strim.DATA.TRIM3_LVDS_TX;
			} else if (g_prod.ST_U8bit.LVDS_TX_TRIM2_VALID) {
				CPE_LOG("LVDS_TX_TRIM2_VALID");
				m_u32Ret = g_strim.DATA.TRIM2_LVDS_TX;
			} else if (g_prod.ST_U8bit.LVDS_TX_TRIM1_VALID) {
				CPE_LOG("LVDS_TX_TRIM1_VALID");
				m_u32Ret = g_strim.DATA.TRIM1_LVDS_TX;
			} else {
				CPE_LOG("LVDS_TX_TRIM Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_USB01: {
			if (g_prod.ST_U8bit.USB_01_TRIM3_VALID) {
				CPE_LOG("USB_01_TRIM3_VALID");
				m_u32Ret = g_strim.DATA.TRIM3_USB_01;
			} else if (g_prod.ST_U8bit.USB_01_TRIM2_VALID) {
				CPE_LOG("USB_01_TRIM2_VALID");
				m_u32Ret = g_strim.DATA.TRIM2_USB_01;
			} else if (g_prod.ST_U8bit.USB_01_TRIM1_VALID) {
				CPE_LOG("USB_01_TRIM1_VALID");
				m_u32Ret = g_strim.DATA.TRIM1_USB_01;
			} else {
				CPE_LOG("EN_CPE_TRIMDATA_TYPE_USB01 Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_ETH_CURRENT: {
			if (g_prod.ST_U8bit.ETH_CURRENT_TRIM3_VALID) {
				CPE_LOG("ETH_CURRENT_TRIM3_VALID");
				m_u32Ret = g_strim.DATA.TRIM3_ETH_CURRENT;
			} else if (g_prod.ST_U8bit.ETH_CURRENT_TRIM2_VALID) {
				CPE_LOG("ETH_CURRENT_TRIM2_VALID");
				m_u32Ret = g_strim.DATA.TRIM2_ETH_CURRENT;
			} else if (g_prod.ST_U8bit.ETH_CURRENT_TRIM1_VALID) {
				CPE_LOG("ETH_CURRENT_TRIM1_VALID");
				m_u32Ret = g_strim.DATA.TRIM1_ETH_CURRENT;
			} else {
				CPE_LOG("ETHERNET_TRIM Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_ETH_RESISTANCE: {
			if (g_prod.ST_U8bit.ETH_RESISTANCE_TRIM3_VALID) {
				CPE_LOG("ETH_RESISTANCE_TRIM3_VALID Zero");
				m_u32Ret = g_strim.DATA.TRIM3_ETH_RESISTANCE;
			} else if (g_prod.ST_U8bit.ETH_RESISTANCE_TRIM2_VALID) {
				CPE_LOG("ETH_RESISTANCE_TRIM2_VALID Zero");
				m_u32Ret = g_strim.DATA.TRIM2_ETH_RESISTANCE;
			} else if (g_prod.ST_U8bit.ETH_RESISTANCE_TRIM1_VALID) {
				CPE_LOG("ETH_RESISTANCE_TRIM1_VALID Zero");
				m_u32Ret = g_strim.DATA.TRIM1_ETH_RESISTANCE;
			} else {
				CPE_LOG("EN_CPE_TRIMDATA_TYPE_RESISTANCE Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;
		}
		case EN_CPE_TRIMDATA_TYPE_USB23: {
			if (g_prod.ST_U8bit.USB_PORT23_TRIM3_VALID) {
				CPE_LOG("USB_PORT23_TRIM3_VALID");
				m_u32Ret = g_strim.DATA.TRIM3_USB_PORT23;
			} else if (g_prod.ST_U8bit.USB_PORT23_TRIM2_VALID) {
				CPE_LOG("USB_PORT23_TRIM2_VALID");
				m_u32Ret = g_strim.DATA.TRIM2_USB_PORT23;
			} else if (g_prod.ST_U8bit.USB_PORT23_TRIM1_VALID) {
				CPE_LOG("USB_PORT23_TRIM1_VALID");
				m_u32Ret = g_strim.DATA.TRIM1_USB_PORT23;
			} else {
				CPE_LOG("EN_CPE_TRIMDATA_TYPE_USB23 Zero");
				m_u32Ret = TRIM_DATA_ERROR;
			}
			break;

		}
		default: {
			CPE_LOG("EN_CPE_TRIMDATA_TYPE_MAX");
			break;
		}
	}
	CPE_LOG(" TrimV = 0x%x %d\n", m_u32Ret, m_u32Ret);
	return m_u32Ret;

}
EXPORT_SYMBOL(ntcpe_read_trimdata);

