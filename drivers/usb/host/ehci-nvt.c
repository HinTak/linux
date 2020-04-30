/*
 * che-chun Kuo <c_c_kuo@novatek.com.tw>
 *
 * This file is licenced under the GPL.
 */

#include "NT72668.h"
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

#if defined(CONFIG_ARCH_NVT72673)
static u32 phy_hi_reset_flag = 0;
#define NVT72673_PHY30_OTHER_CFG_0	0xfd18c158
#endif

#ifdef CONFIG_DMA_COHERENT
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
			| USB_MCFG_PFEN | USB_MCFG_RDCOMB \
			| USB_MCFG_SSDEN | USB_MCFG_UCAM \
			| USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#else
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
			| USB_MCFG_PFEN | USB_MCFG_RDCOMB \
			| USB_MCFG_SSDEN \
			| USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#endif

#if defined(CONFIG_ARCH_NVT72673)
static const struct nt72668_platform_data ehci_pdata[4] = {
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U0,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U0,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U0,
		.clk_src        = EN_SYS_CLK_SRC_USB_U0_U1_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U1,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U1_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U1,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U1,
		.clk_src        = EN_SYS_CLK_SRC_USB_U0_U1_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U3,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U3_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U3,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U3,
		.clk_src        = EN_SYS_CLK_SRC_USB_U2_U3_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U4,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U4_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U4,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U4,
		.clk_src        = EN_SYS_CLK_SRC_USB_U2_U3_30M,
	},
};
#else
static const struct nt72668_platform_data ehci_pdata[4] = {
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U0,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U0_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U0,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U0,
		.clk_src        = EN_SYS_CLK_SRC_USB_U0_U1_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U1,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U1_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U1,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U1,
		.clk_src        = EN_SYS_CLK_SRC_USB_U0_U1_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U2,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U2_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U2,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U2,
		.clk_src        = EN_SYS_CLK_SRC_USB_U2_U3_30M,
	},
	{
		.ahb_usb20      = EN_SYS_CLK_RST_AHB_USB20_U3,
		.ahb_usb20_pclk = EN_SYS_CLK_RST_AHB_USB20_U3_PCLK,
		.axi_usb20      = EN_SYS_CLK_RST_AXI_USB20_U3,
		.core_usb20     = EN_SYS_CLK_RST_CORE_USB20_U3,
		.clk_src        = EN_SYS_CLK_SRC_USB_U2_U3_30M,
	},
};
#endif

/* ======= for SEC EW item ============ */
#if defined(CONFIG_USB_EW_FEATURE)
static int nvt_device_resume_timeout_cnt_get(void *data, u64 *val)
{
	struct ehci_hcd *ehci = (struct ehci_hcd *)data;
	*val = (u64)ehci->device_resume_timeout_cnt;
	return 0;
}
static int nvt_port_status_regval_cnt_get(void *data, u64 *val)
{
	struct ehci_hcd *ehci = (struct ehci_hcd *)data;
	*val = (u64)ehci->port_status_regval_cnt;
	return 0;
}
static int nvt_cerr_count_get(void *data, u64 *val)
{
	struct ehci_hcd *ehci = (struct ehci_hcd *)data;
	*val = (u64)ehci->stats.cerr_count;
	return 0;
}

static int nvt_cerr_count_set(void *data, u64 val)
{
	int v = (int)val;

	if (v < 0 || v > 3)
		return -EINVAL;
	EHCI_TUNE_CERR = v;
	return 0;
}

static int nvt_hanshake_fails_get(void *data, u64 *val)
{
	struct ehci_hcd *ehci = (struct ehci_hcd *)data;
	*val = ehci->stats.hanshake_fails;
	return 0;
}

static int nvt_device_detect_fails_get(void *data, u64 *val)
{
	struct ehci_hcd *ehci = (struct ehci_hcd *)data;
	*val = ehci->stats.device_detect_fails;
	return 0;
}

static int nvt_strength_level_get(void *data, u64 *val)
{
/*	struct ehci_hcd *ehci = (struct ehci_hcd *)data; */
	struct usb_hcd *hcd = ehci_to_hcd(data);
	struct nvt_u2_phy *pphy = container_of(hcd->usb_phy, struct nvt_u2_phy, u_phy);

	*val = (readl(pphy->phy_regs + 0x18) & 0xe) >> 1;
	return 0;
}

static int nvt_strength_level_set(void *data, u64 val)
{
	int v = (int)val;
	struct usb_hcd *hcd = ehci_to_hcd(data);
	struct nvt_u2_phy *pphy = container_of(hcd->usb_phy, struct nvt_u2_phy, u_phy);
	u32 reg_val;

	if (v < 0 || v > 7)
		return -EINVAL;

	reg_val = (readl(pphy->phy_regs + 0x18) & (~0xe)) | (v <<1);
	writel(reg_val, pphy->phy_regs + 0x18);
	return 0;
}
#if defined(CONFIG_ARCH_NVT72673)
static int usb_ioremap_write(u32 address, u32 value)
{
	u32 *remap_address;

	remap_address = ioremap(address, 0x4);
	if (remap_address == NULL)
		return -1;

	writel(value, remap_address);
	iounmap(remap_address);

	return 0;
}
static u32 usb_ioremap_read(u32 address)
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

#endif

static int nvt_disconnect_threshold_get(void *data, u64 *val)
{
	/*	struct ehci_hcd *ehci = (struct ehci_hcd *)data; */
	struct usb_hcd *hcd = ehci_to_hcd(data);
	struct nvt_u2_phy *pphy = container_of(hcd->usb_phy, struct nvt_u2_phy, u_phy);
#if defined(CONFIG_ARCH_NVT72673)
	struct device_node *dn;
	u32 id;

	dn = hcd->self.controller->of_node;

	if (of_property_read_u32(dn, "id", &id) != 0) {
		dev_err(hcd->self.controller, "could not get usb id\n");
		return -EINVAL;
	}

	/* usb port 3,4 (id 2,3) disconnect threshold setting is belonged to different register */
	if (id == 2 || id == 3)
		*val = (usb_ioremap_read(NVT72673_PHY30_OTHER_CFG_0) & 0x30) >> 4;
	else
		*val = (readl(pphy->phy_regs + 0x14) & 0x3);
#else
	*val = (readl(pphy->phy_regs + 0x14) & 0x3);
#endif
	return 0;
}

static int nvt_disconnect_threshold_set(void *data, u64 val)
{
	int v = (int)val;
	struct usb_hcd *hcd = ehci_to_hcd(data);
	struct nvt_u2_phy *pphy = container_of(hcd->usb_phy, struct nvt_u2_phy, u_phy);
	u32 reg_val;
#if defined(CONFIG_ARCH_NVT72673)
	struct device_node *dn;
	u32 id;
#endif

	if (v < 0 || v > 3)
		return -EINVAL;

#if defined(CONFIG_ARCH_NVT72673)
	dn = hcd->self.controller->of_node;

	if (of_property_read_u32(dn, "id", &id) != 0) {
		dev_err(hcd->self.controller, "could not get usb id\n");
		return -EINVAL;
	}

	/* usb port 3,4 (id 2,3) disconnect threshold setting is belonged to different register */
	if (id == 2 || id == 3) {
		reg_val = usb_ioremap_read(NVT72673_PHY30_OTHER_CFG_0);
		reg_val = (reg_val & (~0x30)) | v << 4;
		usb_ioremap_write(NVT72673_PHY30_OTHER_CFG_0, reg_val);
	} else {
		reg_val = (readl(pphy->phy_regs + 0x14) & (~0x3)) | v ;
		writel(reg_val, pphy->phy_regs + 0x14);
	}

#else
	reg_val = (readl(pphy->phy_regs + 0x14) & (~0x3)) | v ;
	writel(reg_val, pphy->phy_regs + 0x14);
#endif
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_cerr_count_fops,
	nvt_cerr_count_get, nvt_cerr_count_set, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_hanshake_fails_fops,
	nvt_hanshake_fails_get, NULL, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_device_detect_fails_fops,
	nvt_device_detect_fails_get, NULL, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_strength_level_fops,
	nvt_strength_level_get, nvt_strength_level_set, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_disconnect_threshold_fops,
	nvt_disconnect_threshold_get, nvt_disconnect_threshold_set, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_nvt_device_resume_timeout_cnt_fops,
	nvt_device_resume_timeout_cnt_get, 0, "%lld\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_nvt_port_status_regval_cnt_fops,
	nvt_port_status_regval_cnt_get, 0, "%lld\n");

void create_ew_debugfs_node(struct ehci_hcd *ehci)
{

	if (ehci->debug_dir) {
		debugfs_create_file("cerr_count",
					S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH,
					ehci->debug_dir, ehci,
					&debug_cerr_count_fops);

		debugfs_create_file("hanshake_fails", S_IRUSR | S_IRGRP | S_IROTH,
					ehci->debug_dir, ehci,
					&debug_hanshake_fails_fops);

		debugfs_create_file("device_detect_fails", S_IRUSR | S_IRGRP | S_IROTH,
					ehci->debug_dir, ehci,
					&debug_device_detect_fails_fops);

		debugfs_create_file("strength_level", S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP| S_IWOTH,
					ehci->debug_dir, ehci,
					&debug_strength_level_fops);

		debugfs_create_file("disconnect_threshold", S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH,
					ehci->debug_dir, ehci,
					&debug_disconnect_threshold_fops);

		debugfs_create_file("resume_timeout_cnt", S_IRUSR | S_IRGRP | S_IROTH,
					ehci->debug_dir, ehci,
					&debug_nvt_device_resume_timeout_cnt_fops);
		debugfs_create_file("port_status_regval_cnt", S_IRUSR | S_IRGRP | S_IROTH,
					ehci->debug_dir, ehci,
					&debug_nvt_port_status_regval_cnt_fops);
	}
}
#endif

/* =============local micon API============= */

enum micom_bt_wifi_idx
{
	MICOM_BT_WIFI_NULL,
	MICOM_WIFI_BOOT,
	MICOM_BT_BOOT,
};

struct micom_bt_wifi_cmd
{
	int idx;
	int length;
	unsigned char cmd;
	unsigned char ack;
	unsigned char data[10];
};

static struct micom_bt_wifi_cmd mbtc[] =
{
	{
		.idx = MICOM_WIFI_BOOT,
		.length = 5,
		.cmd = 0x43,
		.ack = 0x43,
		.data[0] = 0x0,
		.data[1] = 0x0,
		.data[2] = 0x0,
		.data[3] = 0x0,
		.data[4] = 0x0,

	},
	{
		.idx = MICOM_BT_BOOT,
		.length = 5,
		.cmd = 0x44,
		.ack = 0x44,
		.data[0] = 0x0,
		.data[1] = 0x0,
		.data[2] = 0x0,
		.data[3] = 0x0,
		.data[4] = 0x0,

	},
	{
		.idx = MICOM_BT_WIFI_NULL,
	}
};



static unsigned long usb_local_find_symbol(char *name)
{
	struct kernel_symbol *sym = NULL;

	sym = (void *)find_symbol(name, NULL, NULL, 1, true);
	if(sym)
		return sym->value;
	else
		return 0;
}

void usb_local_send_micom(int cmd_type)
{
	unsigned char local_cmd, local_ack;
	char *local_data;
	int ret, len, retry;

	struct micom_bt_wifi_cmd *mbtc_p = mbtc;

	static int (*micom_fn)(char cmd, char ack, char *data, int len) = NULL;
	/* get func pointer */
	if (micom_fn == NULL) {
		micom_fn = (void *)usb_local_find_symbol("sdp_micom_send_cmd_ack");
		if (!micom_fn) {
			printk(KERN_ERR "[%s] can not find a symbol [sdp_micom_send_cmd_ack]\n", __func__);
			return;
		}
	}

	ret = 0;
	while(1) {
		if (mbtc_p[ret].idx == MICOM_BT_WIFI_NULL) {
			printk(KERN_ERR "[%s] fain to find cmd idx %d\n", __func__, cmd_type);
			return;
		}
		if (mbtc_p[ret].idx == cmd_type) {
			local_cmd = mbtc_p[ret].cmd;
			local_data = (char *)mbtc_p[ret].data;
			local_ack = mbtc_p[ret].ack;
			len = mbtc_p[ret].length;
			break;
		}
		ret++;
	}
/*
	printk(KERN_ERR "[%s] sdp_micom_send_cmd_ack cmd %u ack %u data %u %u %u %u %u len %d\n", __func__, local_cmd, local_ack,
		local_data[0], local_data[1], local_data[2], local_data[3], local_data[4], len);
*/
	retry = 3;
	while (1) {
		ret = micom_fn(local_cmd, local_ack, local_data, len);
		if (!ret)
			break;

		printk(KERN_ERR"[%s] sdp_micom_send_cmd_ack fail %d retry %d\n", __func__, ret, retry);

		retry--;
		if (retry == 0) {
			printk(KERN_ERR "[%s] sdp_micom_send_cmd_ack give up \n", __func__);
			return;
		}
	}
/*
	printk(KERN_ERR "[%s] sdp_micom_send_cmd_ack ok \n", __func__);
*/
}

#define USB_HOST_BT_IDX 0
#define USB_HOST_WIFI_IDX 1


static void usb_local_micom_resume_boot(struct platform_device *dev)
{
	struct device_node *dn = dev->dev.of_node;
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	u32 id;
	unsigned long flags;

	if (of_property_read_u32(dn, "id", &id) != 0) {
		dev_err(&dev->dev, "could not get usb id\n");
		return;
	}

	if (id == USB_HOST_BT_IDX) {
		usb_local_send_micom(MICOM_BT_BOOT);
		return;
	}

	if (id == USB_HOST_WIFI_IDX) {
		usb_local_send_micom(MICOM_WIFI_BOOT);
		return;
	}
}



/* ====================================*/
void NT72668_Plat_Resume(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval, retry = 0;

	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	retry = 0;
	do {
		ehci_writel(ehci, INTR_MASK,
		&ehci->regs->intr_enable);
		retval = ehci_handshake(ehci, &ehci->regs->intr_enable,
		    INTR_MASK, INTR_MASK, 250);
		retry++;
	} while (retval != 0);

	if (unlikely(retval != 0))
		ehci_err(ehci, "write fail!\n");
}

static void NT72668_init(struct platform_device *dev)
{
	struct device_node *dn = dev->dev.of_node;
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	u32 id;

	if (of_property_read_u32(dn, "id", &id) != 0) {
		dev_err(&dev->dev, "could not get usb id\n");
		return;
	}

#if defined(CONFIG_ARCH_NVT72673)
		//if usb 2.0 phy group 2 is powered down onboot,
		//we need to reset extra phy setting
		if (id == 2 || id == 3) {
			if (phy_hi_reset_flag == 0) {
				phy_hi_reset_flag = 1;

				/* # set usb30 hclk sw reset*/
				SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB30, true);
				udelay(0x20);

				/* # set usb30 hclk sw clear	*/
				SYS_SetClockReset(EN_SYS_CLK_RST_AHB_USB30, false);
				udelay(0x20);
			}
		}
#endif

	SYS_SetClockReset(ehci_pdata[id].ahb_usb20_pclk, true);
	udelay(0x20);
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20_pclk, false);

	usb_phy_init(hcd->usb_phy);

	/* # set uclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].core_usb20, true);
	udelay(0x20);

	/* # set hclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20, true);
	udelay(0x20);

	/* # set aclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].axi_usb20, true);
	udelay(0x20);

	/* # set aclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].axi_usb20, false);
	udelay(0x20);

	/* # set hclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20, false);
	udelay(0x20);

	/* # set uclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].core_usb20, false);
	udelay(0x20);

	/* #set UCLK to PHY 30M */
	SYS_CLK_SetClockSource(ehci_pdata[id].clk_src, 1);
	udelay(0x20);

	writel(0x20, hcd->regs + 0x100);
	writel(0x0200006e, hcd->regs + 0xe0);
	writel(readl(hcd->regs + 0x84), hcd->regs + 0x84);

	set(0xb, hcd->regs + 0xc4);
	clear(OTGC_INT_B_TYPE, hcd->regs + 0x88);
	set(OTGC_INT_A_TYPE, hcd->regs + 0x88);

	clear(0x20, hcd->regs + 0x80);
	set(0x10, hcd->regs + 0x80);

	/* for full speed device */
	set(1 << 28, hcd->regs + 0x80);

	/* for solving full speed BT disconnect issue */
	writel(0x6000, hcd->regs + 0x44);
}

static void NT72668_stop(struct platform_device *dev)
{
	struct device_node *dn = dev->dev.of_node;
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	u32 id;

	if (of_property_read_u32(dn, "id", &id) != 0) {
		dev_err(&dev->dev, "could not get usb id\n");
		return;
	}

	writel(0x2, hcd->regs + 0x10);
	udelay(1000);

	SYS_SetClockReset(ehci_pdata[id].ahb_usb20_pclk, true);
	udelay(0x20);
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20_pclk, false);

	usb_phy_shutdown(hcd->usb_phy);

	/* # set uclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].core_usb20, true);
	udelay(0x20);

	/* # set hclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20, true);
	udelay(0x20);

	/* # set aclk sw reset */
	SYS_SetClockReset(ehci_pdata[id].axi_usb20, true);
	udelay(0x20);

	/* # set aclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].axi_usb20, false);
	udelay(0x20);

	/* # set hclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].ahb_usb20, false);
	udelay(0x20);

	/* # set uclk sw clear */
	SYS_SetClockReset(ehci_pdata[id].core_usb20, false);
	udelay(0x20);

	/* #set UCLK to PHY 30M */
	SYS_CLK_SetClockSource(ehci_pdata[id].clk_src, 1);
}

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_ehci_NT72668_probe - initialize NT72668-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
static int usb_ehci_NT72668_probe(const struct hc_driver *driver,
			struct usb_hcd **hcd_out,
			struct platform_device *dev)
{
	struct device_node *dn = dev->dev.of_node;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource res;
	u32 irq_affinity;
	int irq;
	int retval;

	retval = of_address_to_resource(dn, 0, &res);
	if (retval) {
		dev_err(&dev->dev, "no address resource provided for index 0");
		return retval;
	}

	/*
	* Right now device-tree probed devices don't get dma_mask set.
	* Since shared usb code relies on it, set it here for now.
	* Once we have dma capability bindings this can go away.
	*/
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	if (!dev->dev.coherent_dma_mask)
		dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

#if defined(CONFIG_USB_EW_FEATURE)
	hcd = usb_create_hcd(driver, &dev->dev, dev_name(&dev->dev));
#else
	hcd = usb_create_hcd(driver, &dev->dev, "NT72668");
#endif
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res.start;
	hcd->rsrc_len = resource_size(&res);

	pr_info("[USB] " NVT_USB_MODIFICATION_TIME
		NVT_USB_MODIFICATION_VERSION " pdev : %s\n", dev->name);

	hcd->regs = devm_ioremap_resource(&dev->dev, &res);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err1;
	}

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	ehci->regs = (void __iomem *)ehci->caps +
	HC_LENGTH(ehci, readl(&ehci->caps->hc_capbase));

	ehci->sbrn = HCD_USB2;

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	irq = irq_of_parse_and_map(dn, 0);
	if (irq == NO_IRQ) {
		dev_err(&dev->dev, "could not get usb irq\n");
		retval = -EBUSY;
		goto err1;
	}

	/* cpu affinity */
	if (!of_property_read_u32(dn, "irq-affinity", &irq_affinity)) {
		if (irq_affinity > 3)
			pr_err("[USB] invalid irq-affinity node value %u\n",
				irq_affinity);
		else {
			retval = irq_set_affinity_hint(irq,
					cpumask_of(irq_affinity));
			if (retval)
				pr_info("[USB] irq %d can't irq_set_affinity %u\n",
					irq, irq_affinity);
		}
	}

	/* get usb phy by device tree node */
	hcd->usb_phy = devm_usb_get_phy_by_phandle(&dev->dev, "usb2phy", 0);
	if (IS_ERR(hcd->phy)) {
		dev_err(&dev->dev, "could not get usb2phy structure, err %ld\n",
				PTR_ERR(up));
		goto err1;
	}

	NT72668_init(dev);

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval == 0) {
		platform_set_drvdata(dev, hcd);
#if defined(CONFIG_USB_EW_FEATURE)
		create_ew_debugfs_node(ehci);
#endif
		usb_phy_vbus_on(hcd->usb_phy);
		usb_local_micom_resume_boot(dev);
		return retval;
	}

	NT72668_stop(dev);

	irq_dispose_mapping(irq);
err1:
	usb_remove_hcd(hcd);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

static void usb_ehci_NT72668_remove(struct usb_hcd *hcd,
			struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	irq_dispose_mapping(hcd->irq);
	pr_debug("calling usb_put_hcd\n");
	NT72668_stop(dev);
	usb_put_hcd(hcd);
}

static int NT72668_ehci_init(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 temp;
	int retval;
	u32 hcc_params;
	struct ehci_qh_hw   *hw;

	spin_lock_init(&ehci->lock);

	ehci->need_io_watchdog = 1;

	hrtimer_init(&ehci->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ehci->hrtimer.function = ehci_hrtimer_func;
	ehci->next_hrtimer_event = EHCI_HRTIMER_NO_EVENT;

	hcc_params = ehci_readl(ehci, &ehci->caps->hcc_params);

	ehci->uframe_periodic_max = 100;

	/*
	* hw default: 1K periodic list heads, one per frame.
	* periodic_size can shrink by USBCMD update if hcc_params allows.
	*/
	ehci->periodic_size = DEFAULT_I_TDPS;

	INIT_LIST_HEAD(&ehci->async_unlink);
	INIT_LIST_HEAD(&ehci->async_idle);
	INIT_LIST_HEAD(&ehci->intr_unlink_wait);
	INIT_LIST_HEAD(&ehci->intr_unlink);
	INIT_LIST_HEAD(&ehci->intr_qh_list);
	INIT_LIST_HEAD(&ehci->cached_itd_list);
	INIT_LIST_HEAD(&ehci->cached_sitd_list);
	INIT_LIST_HEAD(&ehci->tt_list);

	retval = ehci_mem_init(ehci, GFP_KERNEL);
	if (retval < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	hcc_params = ehci_readl(ehci, &ehci->caps->hcc_params);
	if (HCC_ISOC_CACHE(hcc_params))     /*  full frame cache */
		ehci->i_thresh = 8;
	else                    /*  N microframes cached */
		ehci->i_thresh = 2 + HCC_ISOC_THRES(hcc_params);

	/*
	* dedicate a qh for the async ring head, since we couldn't unlink
	* a 'real' qh without stopping the async schedule [4.8].  use it
	* as the 'reclamation list head' too.
	* its dummy is used in hw_alt_next of many tds, to prevent the qh
	* from automatically advancing to the next td after short reads.
	*/
	ehci->async->qh_next.qh = NULL;
	hw = ehci->async->hw;
	hw->hw_next = QH_NEXT(ehci, ehci->async->qh_dma);
	hw->hw_info1 = cpu_to_hc32(ehci, QH_HEAD);
	hw->hw_token = cpu_to_hc32(ehci, QTD_STS_HALT);
	hw->hw_qtd_next = EHCI_LIST_END(ehci);
	ehci->async->qh_state = QH_STATE_LINKED;
	hw->hw_alt_next = QTD_NEXT(ehci, ehci->async->dummy->qtd_dma);

	/* clear interrupt enables, set irq latency */
	if (log2_irq_thresh < 0 || log2_irq_thresh > 6)
		log2_irq_thresh = 0;
	temp = 1 << (16 + log2_irq_thresh);
	ehci->has_ppcd = 0;
	if (HCC_CANPARK(hcc_params)) {
		/* HW default park == 3, on hardware that supports it (like
		* NVidia and ALI silicon), maximizes throughput on the async
		* schedule by avoiding QH fetches between transfers.
		*
		* With fast usb storage devices and NForce2, "park" seems to
		* make problems:  throughput reduction (!), data errors...
		*/
		if (park) {
			park = min_t(unsigned, park, (unsigned) 3);
			temp |= CMD_PARK;
			temp |= park << 8;
		}
	}
	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		temp &= ~(3 << 2);
		temp |= (EHCI_TUNE_FLS << 2);
		switch (EHCI_TUNE_FLS) {
		case 0:
			ehci->periodic_size = 1024; break;
		case 1:
			ehci->periodic_size = 512; break;
		case 2:
			ehci->periodic_size = 256; break;
		default:
			BUG();
		}
	}
	ehci->command = temp;
	hcd->has_tt = 1;
	hcd->self.sg_tablesize = 0;
	return 0;
}


static void NT72668_patch(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	unsigned int    command = ehci_readl(ehci, &ehci->regs->command);
	int retval, retry = 0;

	command |= CMD_RESET;
	ehci_writel(ehci, command, &ehci->regs->command);
	do {
		retval = ehci_handshake(ehci, &ehci->regs->command,
		    CMD_RESET, 0, 250 * 1000);
		retry++;
	} while (retval && (retry < 3));

	if (unlikely(retval != 0 && retry >= 3))
		ehci_err(ehci, "reset fail!\n");

	command = ehci->command;

	ehci_writel(ehci, (command &
			~((unsigned int)(CMD_RUN|CMD_PSE|CMD_ASE))),
			&ehci->regs->command);
	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	retry = 0;
	do {
		ehci_writel(ehci, INTR_MASK,
		&ehci->regs->intr_enable);
		retval = ehci_handshake(ehci, &ehci->regs->intr_enable,
		    INTR_MASK, INTR_MASK, 250);
		retry++;
	} while (retval != 0 && (retry < 3));
	if (unlikely(retval != 0))
		ehci_err(ehci, "write fail!\n");
	ehci->command &= ~((unsigned int)(CMD_PSE|CMD_ASE));
	set_bit(1, &hcd->porcd);
}

static const struct hc_driver ehci_NT72668_hc_driver = {
	.description =      hcd_name,
	.product_desc =     "Novatek EHCI",
	.hcd_priv_size =    sizeof(struct ehci_hcd),

/*
* generic hardware linkage
*/
	.irq =              ehci_irq,
	.flags =            HCD_MEMORY | HCD_USB2 | HCD_BH,

/*
* basic lifecycle operations
*/
	.reset =            NT72668_ehci_init,
	.start =            ehci_run,
	.stop =             ehci_stop,
	.shutdown =         ehci_shutdown,

/*
* managing i/o requests and associated device resources
*/
	.urb_enqueue =      ehci_urb_enqueue,
	.urb_dequeue =      ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset =   ehci_endpoint_reset,

/*
* scheduling support
*/
	.get_frame_number = ehci_get_frame,

/*
* root hub support
*/
	.hub_status_data =  ehci_hub_status_data,
	.hub_control =      ehci_hub_control,
	.bus_suspend =      ehci_bus_suspend,
	.bus_resume =       ehci_bus_resume,
	.relinquish_port =  ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
	.port_nc = NT72668_patch,
};

static int ehci_hcd_NT72668_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	int ret;

	pr_debug("In ehci_hcd_NT72668_drv_probe\n");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_ehci_NT72668_probe(&ehci_NT72668_hc_driver, &hcd, pdev);
	return ret;
}

static int ehci_hcd_NT72668_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	pr_debug("In ehci_hcd_NT72668_drv_remove\n");
	usb_ehci_NT72668_remove(hcd, pdev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int NT72668_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);
	struct platform_device *pdev = to_platform_device(dev);
	int rc;

	rc = ehci_suspend(hcd, do_wakeup);
	disable_irq(hcd->irq);
	NT72668_stop(pdev);
#if defined(CONFIG_ARCH_NVT72673)
		phy_hi_reset_flag = 0;
#endif

	return rc;
}

static int NT72668_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	ehci->device_resume_timeout_cnt = 0;
	ehci->port_status_regval_cnt = 0;

	NT72668_init(pdev);
	NT72668_Plat_Resume(hcd);
	ehci_resume(hcd, false);
	enable_irq(hcd->irq);
	usb_local_micom_resume_boot(pdev);

	return 0;
}
#else
#define NT72668_ehci_suspend    NULL
#define NT72668_ehci_resume     NULL
#endif

static const struct dev_pm_ops NT72668_ehci_pm_ops = {
	.suspend    = NT72668_ehci_suspend,
	.resume     = NT72668_ehci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id NT72668_ehci_match[] = {
	{ .compatible = "nvt,NT72668-ehci" },
	{},
};

MODULE_DEVICE_TABLE(of, NT72668_ehci_match);
#endif

MODULE_ALIAS("NT72668-ehci");
static struct platform_driver ehci_hcd_NT72668_driver = {
	.probe = ehci_hcd_NT72668_drv_probe,
	.remove = ehci_hcd_NT72668_drv_remove,
	.driver = {
		.name = "NT72668-ehci",
		.pm = &NT72668_ehci_pm_ops,
		.of_match_table = of_match_ptr(NT72668_ehci_match),
	}
};

