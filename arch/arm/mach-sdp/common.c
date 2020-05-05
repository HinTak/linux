/*
 * Copyright (C) 2013 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/hardware/gic.h>
#include <asm/arch_timer.h>
#include <mach/map.h>
#include <mach/soc.h>

#define SDP_CHIPID_STATUS		0x00
#define SDP_CHIPID_CTRL			0x04
#define SDP_CHIPID_VALUE_H		0x08
#define SDP_CHIPID_VALUE_L		0x0C
#define SDP_CHIPID_VALUE_MSB		0x10

#define SDP_CHIPID_CTRL_START		(1 << 0)
#define SDP_CHIPID_CTRL_ADJST_TIME	(0xF << 1)

#define SDP_CHIPID_SDP1202	0x1
#define SDP_CHIPID_SDP1304	0x4

static void __iomem *chipid_base;
static unsigned int sdp_chipid = 0;

static enum sdp_board sdp_board_type = 0;

unsigned int sdp_rev(void)
{
	return sdp_chipid;
}
EXPORT_SYMBOL(sdp_rev);

int sdp_get_revision_id(void)
{
	return GET_SDP_REV();
}
EXPORT_SYMBOL(sdp_get_revision_id);

static int __init sdp_check_chipid(void)
{
	u32 val;

	val = SDP_CHIPID_CTRL_ADJST_TIME | SDP_CHIPID_CTRL_START;
	writel(val, chipid_base + SDP_CHIPID_CTRL);

	while (readl(chipid_base + SDP_CHIPID_STATUS))
		;

	sdp_chipid = readl(chipid_base + SDP_CHIPID_VALUE_H);

	if(sdp_chipid == 0) {
		return -1;
	} else
		return 0;
}

static void __init sdp_check_chipid_fromof(void)
{
	if (of_machine_is_compatible("samsung,sdp1202"))
	{
		sdp_chipid = SDP1202_CHIPID << 12;
	}
	if (of_machine_is_compatible("samsung,sdp1302"))
	{
		sdp_chipid = SDP1302_CHIPID << 12;
	}
	if (of_machine_is_compatible("samsung,sdp1304"))
	{
		sdp_chipid = SDP1304_CHIPID << 12;
	}
	if (of_machine_is_compatible("samsung,sdp1307"))
	{
		sdp_chipid = SDP1307_CHIPID << 12;
	}
}

static const struct of_device_id sdp_chipid_of_match[] __initconst = {
	{ .compatible	= "samsung,sdp-chipid", },
	{},
};

int __init sdp_chipid_of_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, sdp_chipid_of_match);
	if (np) {
		chipid_base = of_iomap(np, 0);
		if (!chipid_base)
			panic("Unable to map chipid registers\n");

		if (sdp_check_chipid() == 0)
			goto finish_chipid;
	}

	pr_warn("SDP: cannot get chipid value from HW. Use DT.\n");

	sdp_check_chipid_fromof();

	/* FIXME: temporary, distinguish sdp1302 and sdp1300 */
	if (soc_is_sdp1302()) {
		const char *val;
		if (!of_property_read_string(np, "subid", &val) && !strcmp(val, "mpw"))
			sdp_chipid |= SDP1302MPW_SUBID;
	}	

finish_chipid:

	pr_info("SDP: SoC revision: %08X", sdp_chipid);

	of_node_put(np);

	return 0;
}

static const struct of_device_id sdp_dt_irq_match[] = {
	{ .compatible = "arm,cortex-a15-gic", .data = gic_of_init, },
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
};

void __init sdp_init_irq(void)
{
	printk("[%d] %s\n", __LINE__, __func__);
	if(sdp_chipid == 0)
		sdp_chipid_of_init();
	of_irq_init(sdp_dt_irq_match);
}

void sdp_restart(char mode, const char *cmd)
{
//	sdp_micom_send_cmd(SDP_MICOM_CMD_RESTART, 0, 0);
}


static int __init set_sdp_board_type_maintv(char *p)
{
	sdp_board_type = SDP_BOARD_MAINTV;
	return 0;
}
static int __init set_sdp_board_type_jackpackTV(char *p)
{
	sdp_board_type = SDP_BOARD_JACKPACKTV;
	return 0;
}
static int __init set_sdp_board_type_lfd(char *p)
{
	sdp_board_type = SDP_BOARD_LFD;
	return 0;
}
static int __init set_sdp_board_type_sbb(char *p)
{
	sdp_board_type = SDP_BOARD_SBB;
	return 0;
}
static int __init set_sdp_board_type_hcn(char *p)
{
	sdp_board_type = SDP_BOARD_HCN;
	return 0;
}
static int __init set_sdp_board_type_vgw(char *p)
{
	sdp_board_type = SDP_BOARD_VGW;
	return 0;
}

enum sdp_board get_sdp_board_type(void)
{
	return sdp_board_type;
}

EXPORT_SYMBOL(get_sdp_board_type);

early_param("maintv", set_sdp_board_type_maintv);
early_param("jackpackTV", set_sdp_board_type_jackpackTV);
early_param("lfd", set_sdp_board_type_lfd);
early_param("sbb", set_sdp_board_type_sbb);
early_param("hcn", set_sdp_board_type_hcn);
early_param("vgw", set_sdp_board_type_vgw);

