/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/phy.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/sys_soc.h>
#include <uapi/linux/psci.h>
#include <asm/psci.h>

#include "common.h"
#include "hardware.h"
#include "fsl_sip.h"

struct imx8_soc_data {
	char *name;
	u32 (*soc_revision)(void);
};

static u32 imx8_soc_id;
static u32 imx8_soc_rev = IMX_CHIP_REVISION_UNKNOWN;
static u64 imx8_soc_uid;
static bool m4_is_enabled;
asmlinkage int __invoke_psci_fn_smc(u32, u32, u32, u32);

static ssize_t imx8_get_soc_uid(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%016llX\n", imx8_soc_uid);
}

static struct device_attribute imx8_uid =
	__ATTR(soc_uid, S_IRUGO, imx8_get_soc_uid, NULL);

static inline void imx8_set_soc_revision(u32 rev)
{
	imx8_soc_rev = rev;
}

unsigned int imx8_get_soc_revision(void)
{
	return imx8_soc_rev;
}

static inline void imx8_set_soc_id(u32 id)
{
	imx8_soc_id = id;
}

bool imx8m_src_is_m4_enabled(void)
{
	return m4_is_enabled;
}

int check_m4_enabled(void)
{
	m4_is_enabled = __invoke_psci_fn_smc(FSL_SIP_SRC,
		FSL_SIP_SRC_M4_STARTED, 0, 0);
	if (m4_is_enabled)
		printk("M4 is started\n");

	return 0;
}

static u32 imx_init_revision_from_atf(void)
{
	u32 digprog;
	u32 id, rev;

	digprog = __invoke_psci_fn_smc(FSL_SIP_GET_SOC_INFO, 0, 0, 0);

	/*
	 * Bit [23:16] is the silicon ID
	 * Bit[7:4] is the base layer revision,
	 * Bit[3:0] is the metal layer revision
	 * e.g. 0x10 stands for Tapeout 1.0
	 */

	rev = digprog & 0xff;
	id = digprog >> 16 & 0xff;

	printk("+++%s->digprog:%x\n", __func__,digprog);

	imx8_set_soc_id(id);
	imx8_set_soc_revision(rev);

	return rev;
}

#define OCOTP_UID_LOW	0x410
#define OCOTP_UID_HIGH	0x420

static u64 imx8mm_soc_get_soc_uid(void)
{
	struct device_node *np;
	void __iomem *base;

	u64 val = 0;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx8mm-ocotp");
	if (!np) {
		pr_warn("failed to find ocotp node\n");
		return val;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("failed to map ocotp\n");
		goto put_node;
	}

	val = readl_relaxed(base + OCOTP_UID_HIGH);
	val <<= 32;
	val |=  readl_relaxed(base + OCOTP_UID_LOW);

	iounmap(base);

put_node:
	of_node_put(np);
	return val;
}

static u32 imx8mm_soc_revision(void)
{
	imx8_soc_uid = imx8mm_soc_get_soc_uid();
	return imx_init_revision_from_atf();
}

static int __init imx8_revision_init(void)
{
	struct device_node *root;
	const char *machine;
	u32 rev = IMX_CHIP_REVISION_UNKNOWN;
	int ret;

	root = of_find_node_by_path("/");
	ret = of_property_read_string(root, "model", &machine);
	if (ret)
		return -ENODEV;

	of_node_put(root);

	rev = imx8mm_soc_revision();

	if (rev == IMX_CHIP_REVISION_UNKNOWN)
		pr_info("CPU identified as i.MX8MM, unknown revision\n");
	else
		pr_info("CPU identified as i.MX8MM, silicon rev %d.%d\n",
			(rev >> 4) & 0xf, rev & 0xf);

	return 0;
}
early_initcall(imx8_revision_init);

#define OCOTP_CFG3			0x440
#define OCOTP_CFG3_SPEED_GRADING_SHIFT	8
#define OCOTP_CFG3_SPEED_GRADING_MASK	(0x7 << 8)
#define OCOTP_CFG3_SPEED_2GHZ		4
#define OCOTP_CFG3_SPEED_1P8GHZ		3
#define OCOTP_CFG3_SPEED_1P6GHZ		2
#define OCOTP_CFG3_SPEED_1P2GHZ		1
#define OCOTP_CFG3_SPEED_800MHZ		0
#define OCOTP_CFG3_SPEED_1P0GHZ		1
#define OCOTP_CFG3_SPEED_1P3GHZ		2
#define OCOTP_CFG3_SPEED_1P5GHZ		3
#define OCOTP_CFG3_MKT_SEGMENT_SHIFT	6
#define OCOTP_CFG3_MKT_SEGMENT_MASK	(0x3 << 6)
#define OCOTP_CFG3_CONSUMER		0
#define OCOTP_CFG3_EXT_CONSUMER		1
#define OCOTP_CFG3_INDUSTRIAL		2
#define OCOTP_CFG3_AUTO			3

static void __init imx8mm_opp_check_speed_grading(struct device *cpu_dev)
{
	struct device_node *np;
	void __iomem *base;
	u32 val, market;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx8mm-ocotp");
	if (!np) {
		pr_warn("failed to find ocotp node\n");
		return;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("failed to map ocotp\n");
		goto put_node;
	}
	val = readl_relaxed(base + OCOTP_CFG3);
	/* market segment bit[7:6] */
	market = (val & OCOTP_CFG3_MKT_SEGMENT_MASK)
		>> OCOTP_CFG3_MKT_SEGMENT_SHIFT;
	/* speed grading bit[10:8] */
	val = (val & OCOTP_CFG3_SPEED_GRADING_MASK)
		>> OCOTP_CFG3_SPEED_GRADING_SHIFT;

	switch (market) {
	case OCOTP_CFG3_CONSUMER:
		if (val < OCOTP_CFG3_SPEED_1P8GHZ)
			if (dev_pm_opp_disable(cpu_dev, 1800000000))
				pr_warn("failed to disable 1.8GHz OPP!\n");
		if (val < OCOTP_CFG3_SPEED_1P6GHZ)
			if (dev_pm_opp_disable(cpu_dev, 1600000000))
				pr_warn("failed to disable 1.6GHz OPP!\n");
		break;
	case OCOTP_CFG3_INDUSTRIAL:
		if (dev_pm_opp_disable(cpu_dev, 1800000000))
			pr_warn("failed to disable 1.8GHz OPP!\n");
		if (val < OCOTP_CFG3_SPEED_1P6GHZ)
			if (dev_pm_opp_disable(cpu_dev, 1600000000))
				pr_warn("failed to disable 1.6GHz OPP!\n");
		break;
	default:
		break;
	}

	iounmap(base);

put_node:
	of_node_put(np);
}

static void __init imx8mm_opp_init(void)
{
	struct device_node *np;
	struct device *cpu_dev = get_cpu_device(0);

	if (!cpu_dev) {
		pr_warn("failed to get cpu0 device\n");
		return;
	}
	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		pr_warn("failed to find cpu0 node\n");
		return;
	}

	if (of_init_opp_table(cpu_dev)) {
		pr_warn("failed to init OPP table\n");
		goto put_node;
	}

	imx8mm_opp_check_speed_grading(cpu_dev);

put_node:
	of_node_put(np);
}

static void __init imx8mm_init_machine(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	u32 soc_rev;
	int ret;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	soc_dev_attr->family = "Freescale i.MX";
	soc_dev_attr->soc_id = "i.MX8MM";

	soc_rev = imx8_get_soc_revision();
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d",
					   (soc_rev >> 4) & 0xf,
					    soc_rev & 0xf);
	if (!soc_dev_attr->revision)
		goto free_soc;

	of_property_read_string(of_root, "model", &soc_dev_attr->machine);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		goto free_rev;

	ret = device_create_file(soc_device_to_device(soc_dev), &imx8_uid);
	if (ret)
		pr_err("could not register sysfs entry\n");

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	return;

free_rev:
	kfree(soc_dev_attr->revision);
free_soc:
	kfree(soc_dev_attr);
	return;
}

static void __init imx8mm_init_irq(void)
{
	irqchip_init();
}

static void __init imx8mm_init_late(void)
{
	imx8mm_opp_init();
	platform_device_register_simple("imx8mm-cpufreq", -1, NULL, 0);
}

static void __init imx8mm_map_io(void)
{
	debug_ll_io_init();
}

static const char *imx8mm_dt_compat[] __initconst = {
	"fsl,imx8mm",
	NULL,
};

DT_MACHINE_START(IMX8MM, "Freescale i.MX8 mScale Mini (Device Tree)")
	.init_irq	= imx8mm_init_irq,
	.init_machine	= imx8mm_init_machine,
	.init_late	= imx8mm_init_late,
	.dt_compat	= imx8mm_dt_compat,
	.map_io		= imx8mm_map_io,
MACHINE_END
